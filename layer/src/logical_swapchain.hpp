#ifndef LOGICAL_SWAPCHAIN_HPP_INCLUDED
#define LOGICAL_SWAPCHAIN_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>

#include "effect.hpp"

#include "vulkan_include.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    // for each swapchain, we have the Images and the other stuff we need to execute the compute shader
    struct LogicalSwapchain
    {
        LogicalDevice*                       pLogicalDevice;
        VkSwapchainCreateInfoKHR             swapchainCreateInfo;
        VkExtent2D                           imageExtent;
        VkFormat                             format;
        uint32_t                             imageCount;
        std::vector<VkImage>                 images;
        std::vector<VkImage>                 fakeImages;
        std::vector<VkCommandBuffer>         commandBuffersEffect;
        // betterframe-xp: same job without the shader pass (fake image -> real image copy), submitted instead of
        // commandBuffersEffect while every effect amount is zero. Empty when the device has no mutable-format swapchain.
        std::vector<VkCommandBuffer>         commandBuffersCopy;
        std::shared_ptr<Effect>              copyEffect;
        bool                                 copyPathActive = false; // what the last present submitted, for the log
        std::vector<VkSemaphore>             semaphores;
        std::vector<std::shared_ptr<Effect>> effects;
        VkDeviceMemory                       fakeImageMemory;
        // betterframe-xp: 2 timestamps per image (start/end of our command buffer)
        VkQueryPool                          queryPool = VK_NULL_HANDLE;
        std::vector<bool>                    querySubmitted;
        double                               timestampPeriodNs = 0.0;
        uint64_t                             timestampMask     = 0;

        void destroy();
    };
} // namespace vkBasalt

#endif // LOGICAL_SWAPCHAIN_HPP_INCLUDED
