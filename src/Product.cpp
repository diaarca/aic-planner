#include "Product.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

std::vector<Product> Product::readCSV(const std::string& filename,
                                    const std::map<std::string, double>& mineral_limits)
{
    std::vector<Product> products;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return products;
    }
    std::string line;

    if (!std::getline(file, line))
        return products;

    std::vector<std::string> header = CSVReader::parse_line(line);

    int name_idx = -1, value_idx = -1, time_idx = -1, depot_idx = -1,
        width_idx = -1, height_idx = -1;
    std::vector<std::pair<std::string, int>> mineral_indices;
    std::vector<std::pair<std::string, int>> facility_indices;

    for (int i = 0; i < (int)header.size(); ++i)
    {
        if (header[i] == "product" || header[i] == "Item_Name")
            name_idx = i;
        else if (header[i] == "value" || header[i] == "Trading_Value")
            value_idx = i;
        else if (header[i] == "time" || header[i] == "Production_Time")
            time_idx = i;
        else if (header[i] == "depot")
            depot_idx = i;
        else if (header[i] == "width")
            width_idx = i;
        else if (header[i] == "height")
            height_idx = i;
        else if (mineral_limits.count(header[i]))
        {
            mineral_indices.push_back({header[i], i});
        }
        else
        {
            facility_indices.push_back({header[i], i});
        }
    }

    if (name_idx == -1 || value_idx == -1 || time_idx == -1)
    {
        return products;
    }

    while (std::getline(file, line))
    {
        auto row = CSVReader::parse_line(line);
        if (row.size() != header.size())
            continue;

        Product p;
        p.name = row[name_idx];
        p.value = std::stod(row[value_idx]);
        p.production_time = std::stod(row[time_idx]);
        p.factory_depot = (depot_idx != -1) ? std::stod(row[depot_idx]) : 0;
        p.factory_width = (width_idx != -1) ? std::stod(row[width_idx]) : 0;
        p.factory_height = (height_idx != -1) ? std::stod(row[height_idx]) : 0;

        for (const auto& m_info : mineral_indices)
        {
            p.mineral_consumption[m_info.first] = std::stod(row[m_info.second]);
        }
        for (const auto& f_info : facility_indices)
        {
            p.factory_facilities[f_info.first] = std::stod(row[f_info.second]);
        }
        products.push_back(p);
    }
    return products;
}

void Product::print_table(const std::vector<Product>& products,
                         const std::map<std::string, double>& mineral_limits)
{
    if (products.empty())
        return;

    // Identify all facilities across all products
    std::map<std::string, bool> all_facilities;
    for (const auto& p : products)
    {
        for (const auto& f : p.factory_facilities)
        {
            if (f.second > 0)
                all_facilities[f.first] = true;
        }
    }

    std::cout << "\n--- Products ---\n";
    std::cout << std::left << std::setw(10) << "Product" << "|" << std::right
              << std::setw(3) << "Val" << "|" << std::setw(2) << "T" << "|";

    for (const auto& pair : mineral_limits)
    {
        std::string m = pair.first;
        if (m.length() > 4)
            m = m.substr(0, 4);
        std::cout << std::setw(4) << m << "|";
    }
    std::cout << std::setw(2) << "D" << "|" << std::setw(2) << "W" << "|"
              << std::setw(2) << "H" << "|";

    for (const auto& f : all_facilities)
    {
        std::string fname = f.first;
        if (fname.length() > 4)
            fname = fname.substr(0, 4);
        std::cout << std::setw(4) << fname << "|";
    }

    int total_width = 10 + 1 + 3 + 1 + 2 + 1 + mineral_limits.size() * 5 +
                      3 * 3 + all_facilities.size() * 5;
    std::cout << "\n" << std::string(total_width, '-') << "\n";

    for (const auto& p : products)
    {
        std::cout << std::left << std::setw(10)
                  << (p.name.length() > 10 ? p.name.substr(0, 10) : p.name)
                  << "|" << std::right << std::setw(3) << (int)p.value << "|"
                  << std::setw(2) << (int)p.production_time << "|";

        for (const auto& pair : mineral_limits)
        {
            double cons = 0;
            if (p.mineral_consumption.count(pair.first))
                cons = p.mineral_consumption.at(pair.first);
            std::cout << std::setw(4) << (int)cons << "|";
        }

        std::cout << std::setw(2) << (int)p.factory_depot << "|" << std::setw(2)
                  << (int)p.factory_width << "|" << std::setw(2)
                  << (int)p.factory_height << "|";

        for (const auto& f : all_facilities)
        {
            double val = 0;
            if (p.factory_facilities.count(f.first))
                val = p.factory_facilities.at(f.first);
            std::cout << std::setw(4) << (int)val << "|";
        }
        std::cout << "\n";
    }
}
