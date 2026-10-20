#pragma once

#include <vector>
#include <cstdint>

namespace vkBasalt
{
    const std::vector<uint32_t> full_screen_triangle_vert = {
#include "full_screen_triangle.vert.h"
    };

    const std::vector<uint32_t> postfx_frag = {
#include "postfx.frag.h"
    };

} // namespace vkBasalt
