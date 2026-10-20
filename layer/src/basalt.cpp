#include "vulkan_include.hpp"

#include <atomic>
#include <mutex>
#include <map>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <unistd.h>

#include "util.hpp"

#include "logical_device.hpp"
#include "logical_swapchain.hpp"

#include "image_view.hpp"
#include "sampler.hpp"
#include "framebuffer.hpp"
#include "descriptor_set.hpp"
#include "shader.hpp"
#include "graphics_pipeline.hpp"
#include "command_buffer.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "fake_swapchain.hpp"
#include "renderpass.hpp"
#include "format.hpp"
#include "logger.hpp"

#include "effect.hpp"
#include "effect_postfx.hpp"
#include "effect_transfer.hpp"
#include "frame_stats.hpp"
#include "stream_gate.hpp"

#include <chrono>

#define VKBASALT_NAME "VK_LAYER_BETTERFRAME_XP"

#if defined(__GNUC__) && __GNUC__ >= 4
#define VK_BASALT_EXPORT __attribute__((visibility("default")))
#else
#error "Unsupported platform!"
#endif

namespace vkBasalt
{
    // This experiment must not alter SteamVR's helper processes or vrlink.
    const bool targetProcess = [] {
        char path[4096] = {};
        const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (length <= 0) return false;
        path[length] = '\0';
        const char* basename = std::strrchr(path, '/');
        return std::strcmp(basename ? basename + 1 : path, "vrcompositor") == 0;
    }();

    std::shared_ptr<Config> pConfig = nullptr;

    Logger Logger::s_instance;

    // layer book-keeping information, to store dispatch tables by key
    std::unordered_map<void*, InstanceDispatch>                           instanceDispatchMap;
    std::unordered_map<void*, VkInstance>                                 instanceMap;
    std::unordered_map<void*, uint32_t>                                   instanceVersionMap;
    std::unordered_map<void*, std::shared_ptr<LogicalDevice>>             deviceMap;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<LogicalSwapchain>> swapchainMap;

    std::mutex globalLock;
#ifdef _GCC_
    using scoped_lock __attribute__((unused)) = std::lock_guard<std::mutex>;
#else
    using scoped_lock = std::lock_guard<std::mutex>;
#endif

    template<typename DispatchableType>
    void* GetKey(DispatchableType inst)
    {
        return *(void**) inst;
    }

    // Draw-time hooks (stream gate) run on the compositor's recording threads while vkQueuePresent
    // holds globalLock, so they must not take it. Devices are few and live long: a small table, written under
    // globalLock in vkCreateDevice / vkDestroyDevice and read without a lock.
    struct FastDevice
    {
        std::atomic<void*>                key{nullptr};
        std::atomic<const DeviceDispatch*> vkd{nullptr};
    };
    constexpr int kFastDevices = 8;
    FastDevice    fastDevices[kFastDevices];

    void fastRegister(void* key, const DeviceDispatch* vkd)
    {
        for (auto& slot : fastDevices)
        {
            if (slot.key.load() != nullptr) continue;
            slot.vkd.store(vkd);
            slot.key.store(key, std::memory_order_release);
            return;
        }
    }

    void fastRemove(void* key)
    {
        for (auto& slot : fastDevices)
            if (slot.key.load() == key) slot.key.store(nullptr);
    }

    VkResult VKAPI_CALL vkBasalt_CreateInstance(const VkInstanceCreateInfo*  pCreateInfo,
                                                const VkAllocationCallbacks* pAllocator,
                                                VkInstance*                  pInstance)
    {
        VkLayerInstanceCreateInfo* layerCreateInfo = (VkLayerInstanceCreateInfo*) pCreateInfo->pNext;

        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerInstanceCreateInfo*) layerCreateInfo->pNext;
        }

        Logger::trace("vkCreateInstance");

        if (layerCreateInfo == nullptr)
        {
            // No loader instance create info
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        // move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance) gpa(VK_NULL_HANDLE, "vkCreateInstance");

        VkInstanceCreateInfo modifiedCreateInfo = *pCreateInfo;
        VkApplicationInfo    appInfo;
        if (modifiedCreateInfo.pApplicationInfo)
        {
            appInfo = *(modifiedCreateInfo.pApplicationInfo);
            if (appInfo.apiVersion < VK_API_VERSION_1_1)
            {
                appInfo.apiVersion = VK_API_VERSION_1_1;
            }
        }
        else
        {
            appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pNext              = nullptr;
            appInfo.pApplicationName   = nullptr;
            appInfo.applicationVersion = 0;
            appInfo.pEngineName        = nullptr;
            appInfo.engineVersion      = 0;
            appInfo.apiVersion         = VK_API_VERSION_1_1;
        }

        if (targetProcess)
            modifiedCreateInfo.pApplicationInfo = &appInfo;
        VkResult ret                        = createFunc(&modifiedCreateInfo, pAllocator, pInstance);
        if (ret != VK_SUCCESS)
            return ret;

        // fetch our own dispatch table for the functions we need, into the next layer
        InstanceDispatch dispatchTable;
        fillDispatchTableInstance(*pInstance, gpa, &dispatchTable);

        // store the table by key
        {
            scoped_lock l(globalLock);
            instanceDispatchMap[GetKey(*pInstance)] = dispatchTable;
            instanceMap[GetKey(*pInstance)]         = *pInstance;
            instanceVersionMap[GetKey(*pInstance)]  = modifiedCreateInfo.pApplicationInfo ? modifiedCreateInfo.pApplicationInfo->apiVersion : VK_API_VERSION_1_0;
        }

        return ret;
    }

    void VKAPI_CALL vkBasalt_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        if (!instance)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyInstance");

        InstanceDispatch dispatchTable = instanceDispatchMap[GetKey(instance)];

        dispatchTable.DestroyInstance(instance, pAllocator);

        instanceDispatchMap.erase(GetKey(instance));
        instanceMap.erase(GetKey(instance));
        instanceVersionMap.erase(GetKey(instance));
    }

    VkResult VKAPI_CALL vkBasalt_CreateDevice(VkPhysicalDevice             physicalDevice,
                                              const VkDeviceCreateInfo*    pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDevice*                    pDevice)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkCreateDevice");
        VkLayerDeviceCreateInfo* layerCreateInfo = (VkLayerDeviceCreateInfo*) pCreateInfo->pNext;

        // step through the chain of pNext until we get to the link info
        while (layerCreateInfo
               && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || layerCreateInfo->function != VK_LAYER_LINK_INFO))
        {
            layerCreateInfo = (VkLayerDeviceCreateInfo*) layerCreateInfo->pNext;
        }

        if (layerCreateInfo == nullptr)
        {
            // No loader instance create info
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr   gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        // move chain on for next layer
        layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

        PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice) gipa(VK_NULL_HANDLE, "vkCreateDevice");

        if (!targetProcess)
        {
            VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
            if (ret != VK_SUCCESS) return ret;
            std::shared_ptr<LogicalDevice> pLogicalDevice(new LogicalDevice());
            pLogicalDevice->vki = instanceDispatchMap[GetKey(physicalDevice)];
            pLogicalDevice->device = *pDevice;
            pLogicalDevice->physicalDevice = physicalDevice;
            pLogicalDevice->instance = instanceMap[GetKey(physicalDevice)];
            pLogicalDevice->queue = VK_NULL_HANDLE;
            pLogicalDevice->commandPool = VK_NULL_HANDLE;
            pLogicalDevice->supportsMutableFormat = false;
            fillDispatchTableDevice(*pDevice, gdpa, &pLogicalDevice->vkd);
            deviceMap[GetKey(*pDevice)] = pLogicalDevice;
            fastRegister(GetKey(*pDevice), &pLogicalDevice->vkd);
            return VK_SUCCESS;
        }

        // check and activate extentions
        uint32_t extensionCount = 0;

        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensionProperties(extensionCount);
        instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensionProperties.data());

        bool supportsMutableFormat = false;
        for (VkExtensionProperties properties : extensionProperties)
        {
            if (properties.extensionName == std::string("VK_KHR_swapchain_mutable_format"))
            {
                Logger::debug("device supports VK_KHR_swapchain_mutable_format");
                supportsMutableFormat = true;
                break;
            }
        }

        VkPhysicalDeviceProperties deviceProps;
        instanceDispatchMap[GetKey(physicalDevice)].GetPhysicalDeviceProperties(physicalDevice, &deviceProps);

        VkDeviceCreateInfo       modifiedCreateInfo = *pCreateInfo;
        std::vector<const char*> enabledExtensionNames;
        if (modifiedCreateInfo.enabledExtensionCount)
        {
            enabledExtensionNames = std::vector<const char*>(modifiedCreateInfo.ppEnabledExtensionNames,
                                                             modifiedCreateInfo.ppEnabledExtensionNames + modifiedCreateInfo.enabledExtensionCount);
        }

        if (supportsMutableFormat)
        {
            Logger::debug("activating mutable_format");
            addUniqueCString(enabledExtensionNames, "VK_KHR_swapchain_mutable_format");
        }
        if (deviceProps.apiVersion < VK_API_VERSION_1_2 || instanceVersionMap[GetKey(physicalDevice)] < VK_API_VERSION_1_2)
        {
            addUniqueCString(enabledExtensionNames, "VK_KHR_image_format_list");
        }
        modifiedCreateInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
        modifiedCreateInfo.enabledExtensionCount   = enabledExtensionNames.size();

        // Active needed Features
        VkPhysicalDeviceFeatures deviceFeatures = {};
        if (modifiedCreateInfo.pEnabledFeatures)
        {
            deviceFeatures = *(modifiedCreateInfo.pEnabledFeatures);
        }
        deviceFeatures.shaderImageGatherExtended = VK_TRUE;
        modifiedCreateInfo.pEnabledFeatures      = &deviceFeatures;

        VkResult ret = createFunc(physicalDevice, &modifiedCreateInfo, pAllocator, pDevice);

        if (ret != VK_SUCCESS)
            return ret;

        std::shared_ptr<LogicalDevice> pLogicalDevice(new LogicalDevice());
        pLogicalDevice->vki                   = instanceDispatchMap[GetKey(physicalDevice)];
        pLogicalDevice->device                = *pDevice;
        pLogicalDevice->physicalDevice        = physicalDevice;
        pLogicalDevice->instance              = instanceMap[GetKey(physicalDevice)];
        pLogicalDevice->queue                 = VK_NULL_HANDLE;
        pLogicalDevice->queueFamilyIndex      = 0;
        pLogicalDevice->commandPool           = VK_NULL_HANDLE;
        pLogicalDevice->supportsMutableFormat = supportsMutableFormat;

        fillDispatchTableDevice(*pDevice, gdpa, &pLogicalDevice->vkd);

        uint32_t count;

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, nullptr);

        std::vector<VkQueueFamilyProperties> queueProperties(count);

        pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &count, queueProperties.data());
        // Record how the compositor created its queues (family, count, priorities).
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
        {
            auto&       queueInfo = pCreateInfo->pQueueCreateInfos[i];
            std::string prio;
            for (uint32_t q = 0; q < queueInfo.queueCount; q++)
                prio += (q ? "," : "") + std::to_string(queueInfo.pQueuePriorities[q]);
            Logger::info("betterframe-xp device queues: family " + std::to_string(queueInfo.queueFamilyIndex) + " flags "
                         + std::to_string(queueProperties[queueInfo.queueFamilyIndex].queueFlags) + " count "
                         + std::to_string(queueInfo.queueCount) + " priorities " + prio);
        }
        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
        {
            auto& queueInfo = pCreateInfo->pQueueCreateInfos[i];
            if ((queueProperties[queueInfo.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                pLogicalDevice->vkd.GetDeviceQueue(pLogicalDevice->device, queueInfo.queueFamilyIndex, 0, &pLogicalDevice->queue);

                VkCommandPoolCreateInfo commandPoolCreateInfo;
                commandPoolCreateInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                commandPoolCreateInfo.pNext            = nullptr;
                commandPoolCreateInfo.flags            = 0;
                commandPoolCreateInfo.queueFamilyIndex = queueInfo.queueFamilyIndex;

                Logger::debug("Found graphics capable queue");
                pLogicalDevice->vkd.CreateCommandPool(pLogicalDevice->device, &commandPoolCreateInfo, nullptr, &pLogicalDevice->commandPool);
                pLogicalDevice->queueFamilyIndex = queueInfo.queueFamilyIndex;

                initializeDispatchTable(pLogicalDevice->queue, pLogicalDevice->device);

                break;
            }
        }

        if (!pLogicalDevice->queue)
            Logger::err("Did not find a graphics queue!");

        deviceMap[GetKey(*pDevice)] = pLogicalDevice;
        fastRegister(GetKey(*pDevice), &pLogicalDevice->vkd);

        return VK_SUCCESS;
    }

    void VKAPI_CALL vkBasalt_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device)
            return;

        scoped_lock l(globalLock);

        Logger::trace("vkDestroyDevice");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (pLogicalDevice->commandPool != VK_NULL_HANDLE)
        {
            Logger::debug("DestroyCommandPool");
            pLogicalDevice->vkd.DestroyCommandPool(device, pLogicalDevice->commandPool, pAllocator);
        }

        pLogicalDevice->vkd.DestroyDevice(device, pAllocator);

        fastRemove(GetKey(device));
        deviceMap.erase(GetKey(device));
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR(VkDevice                        device,
                                                               const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                               const VkAllocationCallbacks*    pAllocator,
                                                               VkSwapchainKHR*                 pSwapchain)
    {
        scoped_lock l(globalLock);

        Logger::trace("vkCreateSwapchainKHR");

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (!targetProcess)
        {
            return pLogicalDevice->vkd.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
        }
        Logger::info("betterframe-xp vrcompositor swapchain " + std::to_string(pCreateInfo->imageExtent.width) + "x" + std::to_string(pCreateInfo->imageExtent.height) + " format=" + std::to_string(pCreateInfo->imageFormat));

        VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;

        VkFormat format = modifiedCreateInfo.imageFormat;

        VkFormat srgbFormat  = isSRGB(format) ? format : convertToSRGB(format);
        VkFormat unormFormat = isSRGB(format) ? convertToUNORM(format) : format;
        Logger::debug(std::to_string(srgbFormat) + " " + std::to_string(unormFormat));

        VkFormat formats[] = {unormFormat, srgbFormat};

        VkImageFormatListCreateInfoKHR imageFormatListCreateInfo;
        if (pLogicalDevice->supportsMutableFormat)
        {
            modifiedCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                            | VK_IMAGE_USAGE_SAMPLED_BIT; // we want to use the swapchain images as output of the graphics pipeline
            modifiedCreateInfo.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
            // TODO what if the application already uses multiple formats for the swapchain?

            imageFormatListCreateInfo.sType           = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR;
            imageFormatListCreateInfo.pNext           = modifiedCreateInfo.pNext;
            imageFormatListCreateInfo.viewFormatCount = (srgbFormat == unormFormat) ? 1 : 2;
            imageFormatListCreateInfo.pViewFormats    = formats;

            modifiedCreateInfo.pNext = &imageFormatListCreateInfo;
        }

        modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;


        Logger::debug("format " + std::to_string(modifiedCreateInfo.imageFormat));
        std::shared_ptr<LogicalSwapchain> pLogicalSwapchain(new LogicalSwapchain());
        pLogicalSwapchain->pLogicalDevice      = pLogicalDevice;
        pLogicalSwapchain->swapchainCreateInfo = *pCreateInfo;
        pLogicalSwapchain->imageExtent         = modifiedCreateInfo.imageExtent;
        pLogicalSwapchain->format              = modifiedCreateInfo.imageFormat;
        pLogicalSwapchain->imageCount          = 0;

        // Our copy pass keeps each real image ~2 ms longer, so the compositor's next acquire can stall
        // with only 2 images. One spare image (BETTERFRAME_XP_EXTRA_IMAGES, default 1) absorbs that.
        uint32_t extraImages = 1;
        if (const char* env = std::getenv("BETTERFRAME_XP_EXTRA_IMAGES"))
            extraImages = static_cast<uint32_t>(std::atoi(env));
        VkResult result = VK_ERROR_INITIALIZATION_FAILED;
        if (extraImages > 0)
        {
            VkSwapchainCreateInfoKHR widened = modifiedCreateInfo;
            widened.minImageCount += extraImages;
            result = pLogicalDevice->vkd.CreateSwapchainKHR(device, &widened, pAllocator, pSwapchain);
            Logger::info("betterframe-xp swapchain minImageCount " + std::to_string(widened.minImageCount)
                         + (result == VK_SUCCESS ? " ok" : " refused, using the original count"));
        }
        if (result != VK_SUCCESS)
            result = pLogicalDevice->vkd.CreateSwapchainKHR(device, &modifiedCreateInfo, pAllocator, pSwapchain);
        if (result != VK_SUCCESS)
            return result;


        swapchainMap[*pSwapchain] = pLogicalSwapchain;

        return result;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_GetSwapchainImagesKHR(VkDevice       device,
                                                                  VkSwapchainKHR swapchain,
                                                                  uint32_t*      pCount,
                                                                  VkImage*       pSwapchainImages)
    {
        scoped_lock l(globalLock);
        Logger::trace("vkGetSwapchainImagesKHR " + std::to_string(*pCount));

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();
        if (!targetProcess)
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);

        if (pSwapchainImages == nullptr)
        {
            return pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
        }

        LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();

        // If the images got already requested once, return them again instead of creating new images
        if (pLogicalSwapchain->fakeImages.size())
        {
            *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
            std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
            return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
        }

        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, nullptr);
        pLogicalSwapchain->images.resize(pLogicalSwapchain->imageCount);
        pLogicalDevice->vkd.GetSwapchainImagesKHR(device, swapchain, &pLogicalSwapchain->imageCount, pLogicalSwapchain->images.data());

        // betterframe-xp uses one combined effect. Keep the transfer stage for
        // devices without mutable swapchain images.
        uint32_t fakeImageCount = pLogicalSwapchain->imageCount * (1 + !pLogicalDevice->supportsMutableFormat);

        pLogicalSwapchain->fakeImages =
            createFakeSwapchainImages(pLogicalDevice, pLogicalSwapchain->swapchainCreateInfo, fakeImageCount, pLogicalSwapchain->fakeImageMemory);
        Logger::debug("created fake swapchain images");

        VkFormat unormFormat = convertToUNORM(pLogicalSwapchain->format);
        std::vector<VkImage> firstImages(pLogicalSwapchain->fakeImages.begin(),
                                         pLogicalSwapchain->fakeImages.begin() + pLogicalSwapchain->imageCount);
        std::vector<VkImage> secondImages = pLogicalDevice->supportsMutableFormat
            ? pLogicalSwapchain->images
            : std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount,
                                   pLogicalSwapchain->fakeImages.end());
        pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new PostFxEffect(
            pLogicalDevice, unormFormat, pLogicalSwapchain->imageExtent, firstImages, secondImages, pConfig.get())));

        if (!pLogicalDevice->supportsMutableFormat)
        {
            pLogicalSwapchain->effects.push_back(std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                std::vector<VkImage>(pLogicalSwapchain->fakeImages.end() - pLogicalSwapchain->imageCount, pLogicalSwapchain->fakeImages.end()),
                pLogicalSwapchain->images,
                pConfig.get())));
        }

        Logger::debug("effect count: " + std::to_string(pLogicalSwapchain->effects.size()));

        pLogicalSwapchain->commandBuffersEffect = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        // Plain fake -> real copy, used while the effect pass would change nothing (see QueuePresentKHR).
        // Needs the real images to be the pass's output, i.e. the mutable-format path. Both buffers leave the
        // fake image in PRESENT_SRC and the real image in PRESENT_SRC, so either can follow either.
        if (pLogicalDevice->supportsMutableFormat)
        {
            pLogicalSwapchain->copyEffect = std::shared_ptr<Effect>(new TransferEffect(
                pLogicalDevice,
                pLogicalSwapchain->format,
                pLogicalSwapchain->imageExtent,
                firstImages,
                pLogicalSwapchain->images,
                pConfig.get()));
            pLogicalSwapchain->commandBuffersCopy = allocateCommandBuffer(pLogicalDevice, pLogicalSwapchain->imageCount);
        }
        Logger::debug("allocated ComandBuffers " + std::to_string(pLogicalSwapchain->commandBuffersEffect.size()) + " for swapchain "
                      + convertToString(swapchain));

        // betterframe-xp: GPU timestamps for the panel's load display (skipped if the queue has none)
        {
            VkPhysicalDeviceProperties props;
            pLogicalDevice->vki.GetPhysicalDeviceProperties(pLogicalDevice->physicalDevice, &props);
            uint32_t familyCount = 0;
            pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            pLogicalDevice->vki.GetPhysicalDeviceQueueFamilyProperties(pLogicalDevice->physicalDevice, &familyCount, families.data());
            const uint32_t validBits = pLogicalDevice->queueFamilyIndex < familyCount
                                           ? families[pLogicalDevice->queueFamilyIndex].timestampValidBits
                                           : 0;
            if (validBits > 0 && props.limits.timestampPeriod > 0.0f)
            {
                VkQueryPoolCreateInfo queryInfo = {};
                queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                queryInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
                queryInfo.queryCount = 2 * pLogicalSwapchain->imageCount;
                if (pLogicalDevice->vkd.CreateQueryPool(pLogicalDevice->device, &queryInfo, nullptr, &pLogicalSwapchain->queryPool)
                    != VK_SUCCESS)
                    pLogicalSwapchain->queryPool = VK_NULL_HANDLE;
                pLogicalSwapchain->timestampPeriodNs = props.limits.timestampPeriod;
                pLogicalSwapchain->timestampMask     = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1);
            }
            pLogicalSwapchain->querySubmitted.assign(pLogicalSwapchain->imageCount, false);
            Logger::info(std::string("betterframe-xp gpu timestamps ") + (pLogicalSwapchain->queryPool ? "on" : "unavailable"));
        }

        writeCommandBuffers(pLogicalDevice, pLogicalSwapchain->effects, pLogicalSwapchain->commandBuffersEffect,
                            pLogicalSwapchain->queryPool);
        if (!pLogicalSwapchain->commandBuffersCopy.empty())
            writeCommandBuffers(pLogicalDevice, {pLogicalSwapchain->copyEffect}, pLogicalSwapchain->commandBuffersCopy,
                                pLogicalSwapchain->queryPool);
        Logger::debug("wrote CommandBuffers");

        pLogicalSwapchain->semaphores = createSemaphores(pLogicalDevice, pLogicalSwapchain->imageCount);
        Logger::debug("created semaphores");
        for (unsigned int i = 0; i < pLogicalSwapchain->imageCount; i++)
        {
            Logger::debug(std::to_string(i) + " written commandbuffer " + convertToString(pLogicalSwapchain->commandBuffersEffect[i]));
        }
        Logger::trace("vkGetSwapchainImagesKHR");

        *pCount = std::min<uint32_t>(*pCount, pLogicalSwapchain->imageCount);
        std::memcpy(pSwapchainImages, pLogicalSwapchain->fakeImages.data(), sizeof(VkImage) * (*pCount));
        return *pCount < pLogicalSwapchain->imageCount ? VK_INCOMPLETE : VK_SUCCESS;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        scoped_lock l(globalLock);
        if (!targetProcess)
        {
            return deviceMap[GetKey(queue)]->vkd.QueuePresentKHR(queue, pPresentInfo);
        }
        const auto cpuStart = std::chrono::steady_clock::now();
        static FrameStats stats;
        stats.presentEntered();
        static bool reportedPresent = false;
        if (!reportedPresent) {
            reportedPresent = true;
        }

        LogicalDevice* pLogicalDevice = deviceMap[GetKey(queue)].get();
        static bool reportedQueue = false;
        if (!reportedQueue) {
            Logger::info(std::string("betterframe-xp first intercepted present; present queue ")
                         + (queue == pLogicalDevice->queue ? "is" : "is NOT") + " the queue the effect is submitted to");
            reportedQueue = true;
        }

        std::vector<VkSemaphore> presentSemaphores;
        presentSemaphores.reserve(pPresentInfo->swapchainCount);

        // ALL_COMMANDS (not FRAGMENT_SHADER) so the first GPU timestamp is written only after the
        // compositor's work is done; otherwise the measured time would include waiting for it.
        std::vector<VkPipelineStageFlags> waitStages(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

        for (unsigned int i = 0; i < (*pPresentInfo).swapchainCount; i++)
        {
            uint32_t          index             = (*pPresentInfo).pImageIndices[i];
            VkSwapchainKHR    swapchain         = (*pPresentInfo).pSwapchains[i];
            LogicalSwapchain* pLogicalSwapchain = swapchainMap[swapchain].get();

            // The previous use of this image's command buffer has normally finished by now
            // (the image was acquired again). Read without waiting; skip if not ready.
            if (pLogicalSwapchain->queryPool != VK_NULL_HANDLE && index < pLogicalSwapchain->querySubmitted.size()
                && pLogicalSwapchain->querySubmitted[index])
            {
                uint64_t ts[2] = {0, 0};
                if (pLogicalDevice->vkd.GetQueryPoolResults(pLogicalDevice->device, pLogicalSwapchain->queryPool, 2 * index, 2,
                                                            sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT)
                    == VK_SUCCESS)
                {
                    const uint64_t ticks = (ts[1] - ts[0]) & pLogicalSwapchain->timestampMask;
                    stats.addGpuNs(static_cast<double>(ticks) * pLogicalSwapchain->timestampPeriodNs);
                    stats.noteGpuNs(static_cast<double>(ticks) * pLogicalSwapchain->timestampPeriodNs);
                }
            }

            for (auto& effect : pLogicalSwapchain->effects)
            {
                effect->updateEffect();
            }

            VkSubmitInfo submitInfo;
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.pNext              = nullptr;
            submitInfo.waitSemaphoreCount = i == 0 ? pPresentInfo->waitSemaphoreCount : 0;
            submitInfo.pWaitSemaphores    = i == 0 ? pPresentInfo->pWaitSemaphores : nullptr;
            submitInfo.pWaitDstStageMask  = i == 0 ? waitStages.data() : nullptr;
            submitInfo.commandBufferCount = 1;
            // Nothing to apply right now (home, dashboard without PC video): copy instead of running the shader pass.
            const bool useCopy = !pLogicalSwapchain->commandBuffersCopy.empty() && !pLogicalSwapchain->effects.empty()
                                 && pLogicalSwapchain->effects.front()->isIdle();
            if (useCopy != pLogicalSwapchain->copyPathActive)
            {
                pLogicalSwapchain->copyPathActive = useCopy;
                Logger::info(std::string("betterframe-xp present path: ") + (useCopy ? "plain copy" : "effect pass"));
            }
            submitInfo.pCommandBuffers = useCopy ? &(pLogicalSwapchain->commandBuffersCopy[index])
                                                 : &(pLogicalSwapchain->commandBuffersEffect[index]);
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = &(pLogicalSwapchain->semaphores[index]);

            presentSemaphores.push_back(pLogicalSwapchain->semaphores[index]);

            VkResult vr = pLogicalDevice->vkd.QueueSubmit(pLogicalDevice->queue, 1, &submitInfo, VK_NULL_HANDLE);

            if (vr != VK_SUCCESS)
            {
                return vr;
            }
            if (index < pLogicalSwapchain->querySubmitted.size())
                pLogicalSwapchain->querySubmitted[index] = true;
        }

        VkPresentInfoKHR presentInfo   = *pPresentInfo;
        presentInfo.waitSemaphoreCount = presentSemaphores.size();
        presentInfo.pWaitSemaphores    = presentSemaphores.data();

        // CPU cost = our own work above (the real present below belongs to the compositor)
        stats.addCpuNs(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - cpuStart).count());
        const auto presentStart = std::chrono::steady_clock::now();
        VkResult   presentResult = pLogicalDevice->vkd.QueuePresentKHR(queue, &presentInfo);
        stats.addPresentNs(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - presentStart).count());
        stats.frameDone();
        return presentResult;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
    {
        if (!swapchain)
            return;

        scoped_lock l(globalLock);
        // we need to delete the infos of the oldswapchain

        Logger::trace("vkDestroySwapchainKHR " + convertToString(swapchain));
        if (!targetProcess)
        {
            deviceMap[GetKey(device)]->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
            return;
        }
        swapchainMap[swapchain]->destroy();
        swapchainMap.erase(swapchain);
        LogicalDevice* pLogicalDevice = deviceMap[GetKey(device)].get();

        pLogicalDevice->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////
    // Driver hooks for the stream gate in vrcompositor (STREAM_CALLS), handed out always, so "streamOnly" can
    // change at runtime. Every hook calls the driver with the same arguments; anything else is only a read of them.

    // Lock-free for registered devices: vkQueuePresentKHR holds globalLock while the compositor's draws are
    // recorded around it, so the hooks below must not wait for it. The table is never changed after vkCreateDevice.
    template<typename Dispatchable>
    const DeviceDispatch& hookDispatch(Dispatchable handle)
    {
        void* key = GetKey(handle);
        for (auto& slot : fastDevices)
            if (slot.key.load(std::memory_order_acquire) == key) return *slot.vkd.load(std::memory_order_acquire);
        scoped_lock l(globalLock); // more devices than fastDevices slots: slow path
        return deviceMap[key]->vkd;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
    {
        const VkResult result = hookDispatch(device).CreateImageView(device, pCreateInfo, pAllocator, pView);
        streamGate::onCreateImageView(pCreateInfo, result, result == VK_SUCCESS ? *pView : VK_NULL_HANDLE);
        return result;
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
    {
        streamGate::onBeginCommandBuffer(commandBuffer);
        return hookDispatch(commandBuffer).BeginCommandBuffer(commandBuffer, pBeginInfo);
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
    {
        streamGate::onUpdateDescriptorSets(descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
        hookDispatch(device).UpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
    {
        streamGate::onCmdBindDescriptorSets(commandBuffer, firstSet, descriptorSetCount, pDescriptorSets);
        hookDispatch(commandBuffer).CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        streamGate::onCmdDraw(commandBuffer);
        hookDispatch(commandBuffer).CmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        streamGate::onCmdDrawIndexed(commandBuffer);
        hookDispatch(commandBuffer).CmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
    {
        const VkResult result = hookDispatch(device).AllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
        streamGate::onAllocateDescriptorSets(pAllocateInfo, result, pDescriptorSets);
        return result;
    }

    VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
    {
        streamGate::onDestroyImageView(imageView);
        hookDispatch(device).DestroyImageView(device, imageView, pAllocator);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////
    // Enumeration function

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties)
    {
        if (pPropertyCount)
            *pPropertyCount = 1;

        if (pProperties)
        {
            std::strcpy(pProperties->layerName, VKBASALT_NAME);
            std::strcpy(pProperties->description, "a post processing layer");
            pProperties->implementationVersion = 1;
            pProperties->specVersion           = VK_MAKE_VERSION(1, 2, 0);
        }

        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceLayerProperties(VkPhysicalDevice   physicalDevice,
                                                                uint32_t*          pPropertyCount,
                                                                VkLayerProperties* pProperties)
    {
        return vkBasalt_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateInstanceExtensionProperties(const char*            pLayerName,
                                                                      uint32_t*              pPropertyCount,
                                                                      VkExtensionProperties* pProperties)
    {
        if (pLayerName == NULL || std::strncmp(pLayerName, VKBASALT_NAME, std::strlen(VKBASALT_NAME)))
        {
            return VK_ERROR_LAYER_NOT_PRESENT;
        }

        // don't expose any extensions
        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }

    VkResult VKAPI_CALL vkBasalt_EnumerateDeviceExtensionProperties(VkPhysicalDevice       physicalDevice,
                                                                    const char*            pLayerName,
                                                                    uint32_t*              pPropertyCount,
                                                                    VkExtensionProperties* pProperties)
    {
        // pass through any queries that aren't to us
        if (pLayerName == NULL || std::strncmp(pLayerName, VKBASALT_NAME, std::strlen(VKBASALT_NAME)))
        {
            if (physicalDevice == VK_NULL_HANDLE)
            {
                return VK_SUCCESS;
            }

            scoped_lock l(globalLock);
            return instanceDispatchMap[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(
                physicalDevice, pLayerName, pPropertyCount, pProperties);
        }

        // don't expose any extensions
        if (pPropertyCount)
        {
            *pPropertyCount = 0;
        }
        return VK_SUCCESS;
    }
} // namespace vkBasalt

extern "C"
{ // these are the entry points for the layer, so they need to be c-linkeable

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName);
    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName);

#define GETPROCADDR(func) \
    if (!std::strcmp(pName, "vk" #func)) \
        return (PFN_vkVoidFunction) &vkBasalt::vkBasalt_##func;
    /*
    Return our funktions for the funktions we want to intercept
    the macro takes the name and returns our vkBasalt_##func, if the name is equal
    */

    // vkGetDeviceProcAddr needs to behave like vkGetInstanceProcAddr thanks to some games
#define INTERCEPT_CALLS \
    /* instance chain functions we intercept */ \
    if (!std::strcmp(pName, "vkGetInstanceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetInstanceProcAddr; \
    GETPROCADDR(EnumerateInstanceLayerProperties); \
    GETPROCADDR(EnumerateInstanceExtensionProperties); \
    GETPROCADDR(CreateInstance); \
    GETPROCADDR(DestroyInstance); \
\
    /* device chain functions we intercept*/ \
    if (!std::strcmp(pName, "vkGetDeviceProcAddr")) \
        return (PFN_vkVoidFunction) &vkBasalt_GetDeviceProcAddr; \
    GETPROCADDR(EnumerateDeviceLayerProperties); \
    GETPROCADDR(EnumerateDeviceExtensionProperties); \
    GETPROCADDR(CreateDevice); \
    GETPROCADDR(DestroyDevice); \
    GETPROCADDR(CreateSwapchainKHR); \
    GETPROCADDR(GetSwapchainImagesKHR); \
    GETPROCADDR(QueuePresentKHR); \
    GETPROCADDR(DestroySwapchainKHR); \
\

    // Stream gate hooks (vrcompositor only; the functions themselves return at once elsewhere).
#define STREAM_CALLS \
    GETPROCADDR(CreateImageView) \
    GETPROCADDR(DestroyImageView) \
    GETPROCADDR(AllocateDescriptorSets) \
    GETPROCADDR(UpdateDescriptorSets) \
    GETPROCADDR(BeginCommandBuffer) \
    GETPROCADDR(CmdBindDescriptorSets) \
    GETPROCADDR(CmdDraw) \
    GETPROCADDR(CmdDrawIndexed)

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        if (vkBasalt::pConfig == nullptr)
        {
            vkBasalt::pConfig = std::shared_ptr<vkBasalt::Config>(new vkBasalt::Config());
        }

        INTERCEPT_CALLS

        PFN_vkVoidFunction next;
        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            next = vkBasalt::deviceMap[vkBasalt::GetKey(device)]->vkd.GetDeviceProcAddr(device, pName);
        }
        if (next && vkBasalt::targetProcess)
        {
            STREAM_CALLS
        }
        return next;
    }

    VK_BASALT_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        if (vkBasalt::pConfig == nullptr)
        {
            vkBasalt::pConfig = std::shared_ptr<vkBasalt::Config>(new vkBasalt::Config());
        }

        INTERCEPT_CALLS

        PFN_vkVoidFunction next;
        {
            vkBasalt::scoped_lock l(vkBasalt::globalLock);
            next = vkBasalt::instanceDispatchMap[vkBasalt::GetKey(instance)].GetInstanceProcAddr(instance, pName);
        }
        if (next && vkBasalt::targetProcess)
        {
            STREAM_CALLS
        }
        return next;
    }

} // extern "C"
