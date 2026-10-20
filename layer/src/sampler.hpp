#ifndef SAMPLER_HPP_INCLUDED
#define SAMPLER_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <memory>

#include "vulkan_include.hpp"

#include "logical_device.hpp"

namespace vkBasalt
{
    VkSampler createSampler(LogicalDevice* pLogicalDevice);
} // namespace vkBasalt

#endif // SAMPLER_HPP_INCLUDED
