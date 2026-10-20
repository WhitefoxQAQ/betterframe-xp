#include "config.hpp"

#include <cstdlib>
#include <fstream>

namespace vkBasalt
{
    namespace
    {
        std::string trim(std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }
    }

    Config::Config()
    {
        const char* path = std::getenv("BETTERFRAME_XP_CONFIG_FILE");
        if (!path || !*path) return;
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line))
        {
            const auto comment = line.find('#');
            if (comment != std::string::npos) line.erase(comment);
            const auto equal = line.find('=');
            if (equal != std::string::npos && trim(line.substr(0, equal)) == "lutFile")
            {
                lutFile_ = trim(line.substr(equal + 1));
                return;
            }
        }
    }
} // namespace vkBasalt
