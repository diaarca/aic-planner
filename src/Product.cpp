#include "product.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

std::vector<Product>
Product::readCSV(const std::string& filename,
                 const std::vector<Mineral>& mineral_limits)
{
    auto headers = CSVObject::get_file_headers(filename);
    if (headers.empty()) return {};
    auto products = CSVObject::read_csv_vector<Product>(filename, headers);
    for (auto& p : products)
    {
        p.categorize_consumption(mineral_limits);
    }
    return products;
}

void Product::load(const std::map<std::string, std::string>& row_data)
{
    // Try both naming conventions
    if (row_data.count("product")) name = row_data.at("product");
    else if (row_data.count("Item_Name")) name = row_data.at("Item_Name");

    if (row_data.count("value")) value = std::stod(row_data.at("value"));
    else if (row_data.count("Trading_Value")) value = std::stod(row_data.at("Trading_Value"));

    if (row_data.count("time")) production_time = std::stod(row_data.at("time"));
    else if (row_data.count("Production_Time")) production_time = std::stod(row_data.at("Production_Time"));

    factory_depot = row_data.count("depot") ? std::stod(row_data.at("depot")) : 0;
    factory_width = row_data.count("width") ? std::stod(row_data.at("width")) : 0;
    factory_height = row_data.count("height") ? std::stod(row_data.at("height")) : 0;

    std::vector<std::string> base_keys = {"product", "Item_Name", "value", "Trading_Value", "time", "Production_Time", "depot", "width", "height"};
    for (auto const& [key, val] : row_data)
    {
        bool is_base = false;
        for (const auto& bk : base_keys)
        {
            if (bk == key) { is_base = true; break; }
        }
        if (!is_base)
        {
            _all_dynamic[key] = std::stod(val);
        }
    }
}

void Product::categorize_consumption(const std::vector<Mineral>& mineral_limits)
{
    mineral_consumption.clear();
    factory_facilities.clear();
    for (auto const& [key, val] : _all_dynamic)
    {
        bool is_mineral = std::any_of(mineral_limits.begin(), mineral_limits.end(),
                                     [&](const Mineral& m) { return m.name == key; });
        if (is_mineral)
        {
            mineral_consumption[key] = val;
        }
        else
        {
            factory_facilities[key] = val;
        }
    }
}

std::vector<std::string> Product::get_headers() const
{
    // We'll use the CSV headers we found during load (if we have them) or at least the base ones
    std::vector<std::string> h = {"product", "value", "time", "depot", "width", "height"};
    for (auto const& [name, val] : mineral_consumption) h.push_back(name);
    for (auto const& [name, val] : factory_facilities) h.push_back(name);
    return h;
}


std::vector<std::string> Product::get_values() const
{
    std::vector<std::string> v = {name,
                                  std::to_string(value),
                                  std::to_string(production_time),
                                  std::to_string(factory_depot),
                                  std::to_string(factory_width),
                                  std::to_string(factory_height)};
    for (auto const& [name, val] : mineral_consumption)
    {
        v.push_back(std::to_string(val));
    }
    for (auto const& [name, val] : factory_facilities)
    {
        v.push_back(std::to_string(val));
    }
    return v;
}
