#include "area.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

std::vector<Area> Area::readCSV(const std::string& filename)
{
    auto headers = CSVObject::get_file_headers(filename);
    if (headers.empty())
        return {};
    return CSVObject::read_csv_vector<Area>(filename, headers);
}

void Area::load(const std::map<std::string, std::string>& row_data)
{
    name = row_data.at("area");
    pac_width = std::stod(row_data.at("pac_width"));
    pac_height = std::stod(row_data.at("pac_height"));
    pac_depot_width = std::stod(row_data.at("pac_depot_width"));
    pac_depot_height = std::stod(row_data.at("pac_depot_height"));

    std::vector<std::string> base_keys = {"area", "pac_width", "pac_height",
                                          "pac_depot_width",
                                          "pac_depot_height"};
    for (auto const& [key, val] : row_data)
    {
        bool is_base = false;
        for (const auto& bk : base_keys)
        {
            if (bk == key)
            {
                is_base = true;
                break;
            }
        }
        if (!is_base)
        {
            area_facilities[key] = std::stod(val);
        }
    }
}

std::vector<std::string> Area::get_headers() const
{
    std::vector<std::string> h = {"area", "pac_width", "pac_height",
                                  "pac_depot_width", "pac_depot_height"};
    for (auto const& [name, val] : area_facilities)
    {
        h.push_back(name);
    }
    return h;
}

std::vector<std::string> Area::get_values() const
{
    std::vector<std::string> v = {
        name, std::to_string(pac_width), std::to_string(pac_height),
        std::to_string(pac_depot_width), std::to_string(pac_depot_height)};
    for (auto const& [name, val] : area_facilities)
    {
        v.push_back(std::to_string(val));
    }
    return v;
}
