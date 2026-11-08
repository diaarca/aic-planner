#pragma once

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace aic
{
inline nlohmann::json read_json_file(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open JSON file: " + path.string());
    try
    {
        return nlohmann::json::parse(input);
    }
    catch (const nlohmann::json::parse_error &error)
    {
        throw std::invalid_argument("Invalid JSON in " + path.string() + ": " + error.what());
    }
}
} // namespace aic
