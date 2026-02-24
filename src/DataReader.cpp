#include "DataReader.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
// Helper function to parse a single CSV line
std::vector<std::string> parse_csv_line(const std::string& line)
{
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        // Trim leading and trailing whitespace
        item.erase(0, item.find_first_not_of(" \t\n\r"));
        item.erase(item.find_last_not_of(" \t\n\r") + 1);
        result.push_back(item);
    }
    return result;
}
} // namespace

namespace DataReader
{

std::map<std::string, double> read_minerals(const std::string& filename)
{
    std::map<std::string, double> minerals;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open minerals file: " + filename);
    }
    std::cout << "DEBUG: Reading minerals from: " << filename << std::endl;
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        auto row = parse_csv_line(line);
        if (row.size() >= 2)
        {
            minerals[row[0]] = std::stod(row[1]);
            std::cout << "DEBUG:   Read Mineral: " << row[0]
                      << ", Limit: " << row[1] << std::endl;
        }
    }
    return minerals;
}

std::vector<Product> read_products(const std::string& filename)
{
    std::vector<Product> products;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open products file: " + filename);
    }
    std::cout << "DEBUG: Reading products from: " << filename << std::endl;
    std::string line;

    std::getline(file, line);
    std::vector<std::string> header = parse_csv_line(line);

    int name_idx = -1, value_idx = -1, time_idx = -1;
    std::vector<std::pair<std::string, int>> mineral_indices;

    for (int i = 0; i < header.size(); ++i)
    {
        if (header[i] == "product" || header[i] == "Item_Name")
            name_idx = i;
        else if (header[i] == "value" || header[i] == "Trading_Value")
            value_idx = i;
        else if (header[i] == "time" || header[i] == "Production_Time")
            time_idx = i;
        else
        {
            mineral_indices.push_back({header[i], i});
        }
    }

    if (name_idx == -1 || value_idx == -1 || time_idx == -1)
    {
        throw std::runtime_error(
            "Products CSV header is missing required columns "
            "(product/Item_Name, value/Trading_Value, time/Production_Time).");
    }

    while (std::getline(file, line))
    {
        auto row = parse_csv_line(line);
        if (row.size() != header.size())
            continue;

        Product p;
        p.name = row[name_idx];
        p.value = std::stod(row[value_idx]);
        p.production_time = std::stod(row[time_idx]);

        for (const auto& mineral_info : mineral_indices)
        {
            p.mineral_consumption[mineral_info.first] =
                std::stod(row[mineral_info.second]);
        }
        products.push_back(p);
    }
    return products;
}

std::vector<Area> read_areas(const std::string& filename)
{
    std::vector<Area> areas;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open areas file: " + filename);
    }
    std::cout << "DEBUG: Reading areas from: " << filename << std::endl;
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        auto row = parse_csv_line(line);
        if (row.size() >= 7)
        {
            Area a;
            a.name = row[0];
            a.area_facilities["zipline"] = std::stod(row[1]);
            a.area_facilities["defense"] = std::stod(row[2]);
            a.area_facilities["mining_rig"] = std::stod(row[3]); // "mining" in CSV
            a.pac_depot = std::stod(row[4]);
            a.pac_width = std::stod(row[5]);
            a.pac_height = std::stod(row[6]);
            areas.push_back(a);
            std::cout << "DEBUG:   Read Area: " << a.name
                      << ", Width: " << a.pac_width
                      << ", Height: " << a.pac_height << std::endl;
        }
    }
    return areas;
}

void update_products_with_factories(std::vector<Product>& products,
                                    const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open factories file: " + filename);
    }
    std::cout << "DEBUG: Reading factories from: " << filename << std::endl;
    std::string line;

    if (!std::getline(file, line))
        return;

    std::vector<std::string> header = parse_csv_line(line);

    struct FactoryEntry
    {
        double depot, width, height;
        std::map<std::string, double> facilities;
    };
    std::map<std::string, FactoryEntry> factory_data;

    while (std::getline(file, line))
    {
        auto row = parse_csv_line(line);
        if (row.empty())
            continue;

        FactoryEntry entry;
        std::string name = row[0];
        entry.depot = (row.size() > 1) ? std::stod(row[1]) : 0;
        entry.width = (row.size() > 2) ? std::stod(row[2]) : 0;
        entry.height = (row.size() > 3) ? std::stod(row[3]) : 0;

        for (size_t i = 4; i < row.size() && i < header.size(); ++i)
        {
            entry.facilities[header[i]] = std::stod(row[i]);
        }
        factory_data[name] = entry;
    }

    for (auto& p : products)
    {
        if (factory_data.count(p.name))
        {
            p.factory_depot = factory_data[p.name].depot;
            p.factory_width = factory_data[p.name].width;
            p.factory_height = factory_data[p.name].height;
            p.factory_facilities = factory_data[p.name].facilities;
        }
    }
}

std::map<std::string, double> read_facilities(const std::string& filename)
{
    std::map<std::string, double> facilities;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open facilities file: " + filename);
    }
    std::cout << "DEBUG: Reading facilities from: " << filename << std::endl;
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        auto row = parse_csv_line(line);
        if (row.size() >= 2)
        {
            facilities[row[0]] = std::stod(row[1]);
        }
    }
    return facilities;
}

void update_products_with_fuels(std::vector<Product>& products,
                                const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open fuels file: " + filename);
    }
    std::cout << "DEBUG: Reading fuels from: " << filename << std::endl;
    std::string line;
    std::getline(file, line); // Skip header

    struct FuelEntry
    {
        double power, duration;
    };
    std::map<std::string, FuelEntry> fuel_data;

    while (std::getline(file, line))
    {
        auto row = parse_csv_line(line);
        if (row.size() >= 3)
        {
            fuel_data[row[0]] = {std::stod(row[1]), std::stod(row[2])};
        }
    }

    for (auto& p : products)
    {
        if (fuel_data.count(p.name))
        {
            p.is_fuel = true;
            p.fuel_power = fuel_data[p.name].power;
            p.fuel_duration = fuel_data[p.name].duration;
        }
    }
}

} // namespace DataReader
