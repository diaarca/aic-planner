#include "Area.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

std::vector<Area> Area::readCSV(const std::string& filename)
{
    std::vector<Area> areas;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return areas;
    }
    std::string line;
    if (!std::getline(file, line))
        return areas;

    while (std::getline(file, line))
    {
        auto row = CSVReader::parse_line(line);
        if (row.size() >= 7)
        {
            Area a;
            a.name = row[0];
            a.area_facilities["zipline"] = std::stod(row[1]);
            a.area_facilities["defense"] = std::stod(row[2]);
            a.area_facilities["mining_rig"] = std::stod(row[3]);
            a.pac_depot = std::stod(row[4]);
            a.pac_width = std::stod(row[5]);
            a.pac_height = std::stod(row[6]);
            areas.push_back(a);
        }
    }
    return areas;
}

void Area::print_table(const std::vector<Area>& areas)
{
    if (areas.empty())
        return;

    // Identify all facilities across all areas
    std::map<std::string, bool> all_facilities;
    for (const auto& a : areas)
    {
        for (const auto& f : a.area_facilities)
        {
            if (f.second > 0)
                all_facilities[f.first] = true;
        }
    }

    std::cout << "\n--- Areas ---\n";
    std::cout << std::left << std::setw(15) << "Area" << " | ";
    for (const auto& f : all_facilities)
    {
        std::string fname = f.first;
        if (fname.length() > 5)
            fname = fname.substr(0, 5);
        std::cout << std::setw(6) << fname << " | ";
    }
    std::cout << std::setw(3) << "D" << " | " << std::setw(3) << "W" << " | "
              << std::setw(3) << "H" << "\n";
    std::cout << std::string(60, '-') << "\n";

    for (const auto& a : areas)
    {
        std::cout << std::left << std::setw(15)
                  << (a.name.length() > 15 ? a.name.substr(0, 15) : a.name)
                  << " | ";
        for (const auto& f : all_facilities)
        {
            double val = 0;
            if (a.area_facilities.count(f.first))
                val = a.area_facilities.at(f.first);
            std::cout << std::setw(6) << val << " | ";
        }
        std::cout << std::setw(3) << a.pac_depot << " | " << std::setw(3)
                  << a.pac_width << " | " << std::setw(3) << a.pac_height
                  << "\n";
    }
}
