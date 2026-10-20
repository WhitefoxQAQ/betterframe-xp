#include "logical_swapchain.hpp"

namespace vkBasalt
{
    void LogicalSwapchain::destroy()
    {
        if (imageCount > 0)
        {
            effects.clear();
            copyEffect.reset();

            pLogicalDevice->vkd.FreeCommandBuffers(
                pLogicalDevice->device, pLogicalDevice->commandPool, commandBuffersEffect.size(), commandBuffersEffect.data());
            if (!commandBuffersCopy.empty())
                pLogicalDevice->vkd.FreeCommandBuffers(
                    pLogicalDevice->device, pLogicalDevice->commandPool, commandBuffersCopy.size(), commandBuffersCopy.data());
            Logger::debug("after free commandbuffer");

            if (queryPool != VK_NULL_HANDLE)
                pLogicalDevice->vkd.DestroyQueryPool(pLogicalDevice->device, queryPool, nullptr);

            pLogicalDevice->vkd.FreeMemory(pLogicalDevice->device, fakeImageMemory, nullptr);

            for (uint32_t i = 0; i < fakeImages.size(); i++)
            {
                pLogicalDevice->vkd.DestroyImage(pLogicalDevice->device, fakeImages[i], nullptr);
            }

            for (unsigned int i = 0; i < imageCount; i++)
            {
                pLogicalDevice->vkd.DestroySemaphore(pLogicalDevice->device, semaphores[i], nullptr);
            }
            Logger::debug("after DestroySemaphore");
        }
    }
} // namespace vkBasalt
