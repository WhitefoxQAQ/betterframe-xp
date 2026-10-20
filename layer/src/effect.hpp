#ifndef EFFECT_HPP_INCLUDED
#define EFFECT_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <unordered_map>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    class Effect
    {
    public:
        void virtual applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) = 0;
        void virtual updateEffect(){};
        // True if the effect, as set by the last updateEffect(), leaves the picture unchanged.
        bool virtual isIdle() const { return false; }
        virtual ~Effect(){};

    private:
    };
} // namespace vkBasalt

#endif // EFFECT_HPP_INCLUDED
