#include "stream_gate.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <set>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vkBasalt::streamGate
{
    namespace
    {
        // Only vrcompositor draws the PC video; everywhere else every function here returns at once.
        const bool kCompositor = [] {
            char path[4096] = {};
            const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
            if (length <= 0) return false;
            path[length] = '\0';
            const char* basename = std::strrchr(path, '/');
            return std::strcmp(basename ? basename + 1 : path, "vrcompositor") == 0;
        }();

        // How long after the last PC-video draw the video still counts as on screen. While the video is shown,
        // every compositor frame draws it, so this only has to cover the skew between recording and presenting.
        constexpr int64_t kHoldMs = 200;

        // Descriptor-set indices per pipeline layout are at most 32 in Vulkan.
        constexpr uint32_t kMaxSetIndex = 32;

        std::mutex lock;

        // Views created with a YCbCr conversion: how a shader reads the decoded PC video.
        std::unordered_set<VkImageView> yuvViews;
        // Descriptor set -> binding/element keys that currently hold such a view. Every write replaces the
        // keys it covers, so an overwritten descriptor stops counting at once.
        std::unordered_map<VkDescriptorSet, std::set<uint64_t>> setYuv;
        // Command buffer -> the set bound at each index, with whether it held the video at bind time. The draw
        // reads this snapshot instead of looking the set up again, so the answer is the one the bind saw.
        struct Bound
        {
            VkDescriptorSet set = VK_NULL_HANDLE;
            bool            yuv = false;
        };
        std::unordered_map<VkCommandBuffer, std::vector<Bound>> boundSets;

        std::atomic<int64_t>  lastVideoMs{-1000000};
        std::atomic<uint64_t> videoDraws{0};

        int64_t nowMs()
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }

        uint64_t key(uint32_t binding, uint32_t element)
        {
            return (static_cast<uint64_t>(binding) << 32) | element;
        }

        bool isImageDescriptor(VkDescriptorType type)
        {
            return type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                   || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        }

        // Called with the lock held.
        void setDescriptor(VkDescriptorSet set, uint64_t k, bool yuv)
        {
            if (yuv)
            {
                setYuv[set].insert(k);
                return;
            }
            const auto it = setYuv.find(set);
            if (it == setYuv.end()) return;
            it->second.erase(k);
            if (it->second.empty()) setYuv.erase(it);
        }
    } // namespace

    void onCreateImageView(const VkImageViewCreateInfo* ci, VkResult result, VkImageView view)
    {
        if (!kCompositor || !ci || result != VK_SUCCESS) return;
        bool conversion = false;
        for (auto* p = static_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext)
            if (p->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO) conversion = true;
        std::lock_guard<std::mutex> l(lock);
        if (conversion)
            yuvViews.insert(view);
        else
            yuvViews.erase(view); // a handle can be reused: a plain view must clear an old entry
    }

    void onDestroyImageView(VkImageView view)
    {
        if (!kCompositor || !view) return;
        std::lock_guard<std::mutex> l(lock);
        yuvViews.erase(view);
    }

    void onAllocateDescriptorSets(const VkDescriptorSetAllocateInfo* ai, VkResult result, const VkDescriptorSet* sets)
    {
        if (!kCompositor || !ai || result != VK_SUCCESS || !sets) return;
        // A new set is empty, whatever an earlier set with the same handle held.
        std::lock_guard<std::mutex> l(lock);
        for (uint32_t i = 0; i < ai->descriptorSetCount; ++i)
            setYuv.erase(sets[i]);
    }

    void onUpdateDescriptorSets(uint32_t writeCount, const VkWriteDescriptorSet* writes, uint32_t copyCount, const VkCopyDescriptorSet* copies)
    {
        if (!kCompositor) return;
        std::lock_guard<std::mutex> l(lock);
        for (uint32_t w = 0; writes && w < writeCount; ++w)
        {
            const VkWriteDescriptorSet& write  = writes[w];
            const bool                  images = isImageDescriptor(write.descriptorType) && write.pImageInfo;
            for (uint32_t i = 0; i < write.descriptorCount; ++i)
            {
                const bool yuv = images && yuvViews.count(write.pImageInfo[i].imageView) != 0;
                setDescriptor(write.dstSet, key(write.dstBinding, write.dstArrayElement + i), yuv);
            }
        }
        for (uint32_t c = 0; copies && c < copyCount; ++c)
        {
            const VkCopyDescriptorSet& copy = copies[c];
            const auto                 src  = setYuv.find(copy.srcSet);
            for (uint32_t i = 0; i < copy.descriptorCount; ++i)
            {
                const bool yuv = src != setYuv.end() && src->second.count(key(copy.srcBinding, copy.srcArrayElement + i)) != 0;
                setDescriptor(copy.dstSet, key(copy.dstBinding, copy.dstArrayElement + i), yuv);
            }
        }
    }

    void onBeginCommandBuffer(VkCommandBuffer cb)
    {
        if (!kCompositor) return;
        std::lock_guard<std::mutex> l(lock);
        boundSets.erase(cb); // a new recording starts with nothing bound
    }

    void onCmdBindDescriptorSets(VkCommandBuffer cb, uint32_t firstSet, uint32_t setCount, const VkDescriptorSet* sets)
    {
        if (!kCompositor || !sets || firstSet + setCount > kMaxSetIndex) return;
        std::lock_guard<std::mutex> l(lock);
        auto& bound = boundSets[cb];
        if (bound.size() < firstSet + setCount) bound.resize(firstSet + setCount);
        for (uint32_t i = 0; i < setCount; ++i)
            bound[firstSet + i] = {sets[i], setYuv.count(sets[i]) != 0};
    }

    void onCmdDraw(VkCommandBuffer cb)
    {
        if (!kCompositor) return;
        bool video = false;
        {
            std::lock_guard<std::mutex> l(lock);
            const auto it = boundSets.find(cb);
            if (it == boundSets.end()) return;
            for (const Bound& b : it->second)
                if (b.yuv)
                {
                    video = true;
                    break;
                }
        }
        if (!video) return;
        lastVideoMs.store(nowMs(), std::memory_order_relaxed);
        videoDraws.fetch_add(1, std::memory_order_relaxed);
    }

    // vkCmdDrawIndexed: the same detection as vkCmdDraw.
    void onCmdDrawIndexed(VkCommandBuffer cb)
    {
        onCmdDraw(cb);
    }

    bool videoRecent() { return kCompositor && nowMs() - lastVideoMs.load(std::memory_order_relaxed) < kHoldMs; }

    uint64_t videoDrawCount() { return videoDraws.load(std::memory_order_relaxed); }
} // namespace vkBasalt::streamGate
