#pragma once

#include <string>

namespace vkBasalt
{
    class Config
    {
    public:
        Config();
        const std::string& lutFile() const { return lutFile_; }

    private:
        std::string lutFile_;
    };
} // namespace vkBasalt
