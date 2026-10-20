#include "command_buffer.hpp"

#include "util.hpp"

namespace vkBasalt
{
    std::vector<VkCommandBuffer> allocateCommandBuffer(LogicalDevice* pLogicalDevice, uint32_t count)
    {
        std::vector<VkCommandBuffer> commandBuffers(count);

        VkCommandBufferAllocateInfo allocInfo;
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.pNext              = nullptr;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = pLogicalDevice->commandPool;
        allocInfo.commandBufferCount = count;

        VkResult result = pLogicalDevice->vkd.AllocateCommandBuffers(pLogicalDevice->device, &allocInfo, commandBuffers.data());
        ASSERT_VULKAN(result);
        for (uint32_t i = 0; i < count; i++)
        {
            // initialize dispatch tables for commandBuffers since the are dispatchable objects
            initializeDispatchTable(commandBuffers[i], pLogicalDevice->device);
        }

        return commandBuffers;
    }
    void writeCommandBuffers(LogicalDevice*                                 pLogicalDevice,
                             std::vector<std::shared_ptr<vkBasalt::Effect>> effects,
                             std::vector<VkCommandBuffer>                   commandBuffers,
                             VkQueryPool                                    queryPool)
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext            = nullptr;
        beginInfo.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;

        for (uint32_t i = 0; i < commandBuffers.size(); i++)
        {
            VkResult result = pLogicalDevice->vkd.BeginCommandBuffer(commandBuffers[i], &beginInfo);
            ASSERT_VULKAN(result);
            // betterframe-xp: timestamps 2i / 2i+1 bracket our work (read back on a later present)
            if (queryPool != VK_NULL_HANDLE)
            {
                pLogicalDevice->vkd.CmdResetQueryPool(commandBuffers[i], queryPool, 2 * i, 2);
                pLogicalDevice->vkd.CmdWriteTimestamp(commandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 2 * i);
            }
            for (const auto& effect : effects)
                effect->applyEffect(i, commandBuffers[i]);
            if (queryPool != VK_NULL_HANDLE)
                pLogicalDevice->vkd.CmdWriteTimestamp(commandBuffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 2 * i + 1);
            result = pLogicalDevice->vkd.EndCommandBuffer(commandBuffers[i]);
            ASSERT_VULKAN(result);
        }
    }

    std::vector<VkSemaphore> createSemaphores(LogicalDevice* pLogicalDevice, uint32_t count)
    {
        std::vector<VkSemaphore> semaphores(count);
        VkSemaphoreCreateInfo    info;
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = 0;

        for (uint32_t i = 0; i < count; i++)
        {
            pLogicalDevice->vkd.CreateSemaphore(pLogicalDevice->device, &info, nullptr, &semaphores[i]);
        }
        return semaphores;
    }

} // namespace vkBasalt
