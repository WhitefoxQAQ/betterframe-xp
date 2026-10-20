#ifndef STREAM_GATE_HPP_INCLUDED
#define STREAM_GATE_HPP_INCLUDED

#include <cstdint>

#include "vulkan_include.hpp"

// Tells the post-fx pass whether the compositor is drawing the decoded PC video right now.
//
// vrcompositor reads the video through a multi-planar (YUV) image view that carries a YCbCr conversion.
// The chain view -> descriptor set -> command buffer -> draw is followed here, exactly (a set stops
// counting as soon as the YUV view is overwritten, and a handle that is allocated again starts clean).
// Nothing is changed in the compositor's calls; the hooks only read their arguments.
//
// Used by "streamOnly = 1": effects run only while a PC-video draw was recorded in the last
// kHoldMs milliseconds.
namespace vkBasalt::streamGate
{
    void onCreateImageView(const VkImageViewCreateInfo* pCreateInfo, VkResult result, VkImageView view);
    void onDestroyImageView(VkImageView view);
    void onAllocateDescriptorSets(const VkDescriptorSetAllocateInfo* pAllocateInfo, VkResult result, const VkDescriptorSet* pSets);
    void onUpdateDescriptorSets(uint32_t writeCount, const VkWriteDescriptorSet* pWrites, uint32_t copyCount, const VkCopyDescriptorSet* pCopies);
    void onBeginCommandBuffer(VkCommandBuffer commandBuffer);
    void onCmdBindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t firstSet, uint32_t setCount, const VkDescriptorSet* pSets);
    void onCmdDraw(VkCommandBuffer commandBuffer);
    void onCmdDrawIndexed(VkCommandBuffer commandBuffer);

    // True if a PC-video draw was recorded within the last hold time. Lock-free.
    bool videoRecent();
    // Number of PC-video draws recorded so far (for the log line on state changes).
    uint64_t videoDrawCount();
} // namespace vkBasalt::streamGate

#endif // STREAM_GATE_HPP_INCLUDED
