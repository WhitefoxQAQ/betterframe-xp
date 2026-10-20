#ifndef FORMAT_HPP_INCLUDED
#define FORMAT_HPP_INCLUDED
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
    // Returns a matching sRGB format to a UNORM format if it exist, else returns format
    VkFormat convertToSRGB(VkFormat format);
    // Returns a matching UNORM format to a sRGB format if it exist, else returns format
    VkFormat convertToUNORM(VkFormat format);
    // Returns true if format is SRGB
    bool isSRGB(VkFormat format);
} // namespace vkBasalt

#endif // FORMAT_HPP_INCLUDED
