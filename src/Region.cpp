#include "region.hpp"
#include <iostream>

Region Region::readCSV(const std::string& filename)
{
    Region region;
    auto data = CSVObject::read_file(filename);
    if (data.empty())
        return region;

    for (size_t i = 1; i < data.size(); ++i)
    {
        if (data[i].size() < 2)
            continue;

        if (data[i][0] == "base_power")
        {
            region.base_power = std::stod(data[i][1]);
        }
        else if (data[i][0] == "storage")
        {
            region.storage = std::stod(data[i][1]);
        }
    }
    return region;
}

void Region::load(const std::map<std::string, std::string>& row_data)
{
    if (row_data.count("base_power"))
    {
        base_power = std::stod(row_data.at("base_power"));
    }
    if (row_data.count("storage"))
    {
        storage = std::stod(row_data.at("storage"));
    }
}

std::vector<std::string> Region::get_headers() const
{
    return {"base_power", "storage"};
}

std::vector<std::string> Region::get_values() const
{
    return {std::to_string(base_power), std::to_string(storage)};
}
