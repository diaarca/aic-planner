#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace CSVReader
{
inline std::vector<std::string> parse_line(const std::string& line)
{
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        // Trim leading and trailing whitespace
        const std::string whitespace = " \t\n\r";
        size_t first = item.find_first_not_of(whitespace);
        if (std::string::npos == first)
        {
            result.push_back("");
            continue;
        }
        size_t last = item.find_last_not_of(whitespace);
        result.push_back(item.substr(first, (last - first + 1)));
    }
    return result;
}
} // namespace CSVReader
