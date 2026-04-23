#include "area.hpp"
#include <csv.h>
#include <iomanip>
#include <iostream>
#include <set>

std::vector<Area> Area::readCSV(const std::string& filename)
{
    std::vector<Area> areas;
    io::CSVReader<8> in(filename);
    in.read_header(io::ignore_extra_column, "area", "zipline", "defense",
                   "mining_rig", "pac_depot_width", "pac_depot_height",
                   "pac_width", "pac_height");

    std::string name;
    double zipline, defense, mining_rig, pac_depot_width, pac_depot_height,
        pac_width, pac_height;

    while (in.read_row(name, zipline, defense, mining_rig, pac_depot_width,
                       pac_depot_height, pac_width, pac_height))
    {
        Area area;
        area.name = name;
        area.pac_width = pac_width;
        area.pac_height = pac_height;
        area.pac_depot_width = pac_depot_width;
        area.pac_depot_height = pac_depot_height;
        area.area_facilities["zipline"] = zipline;
        area.area_facilities["defense"] = defense;
        area.area_facilities["mining_rig"] = mining_rig;
        areas.push_back(area);
    }
    return areas;
}

std::ostream& operator<<(std::ostream& os, const Area& a)
{
    os << "Area: " << std::left << std::setw(20) << a.name
       << " | Size: " << a.pac_width << "x" << a.pac_height;
    return os;
}

void Area::print_table(const std::vector<Area>& areas)
{
    std::cout << "\n--- Areas Table ---\n";
    std::cout << std::left << std::setw(20) << "Area"
              << " | " << std::setw(10) << "Width"
              << " | " << std::setw(10) << "Height"
              << " | " << std::setw(8) << "Zip"
              << " | " << std::setw(8) << "Def"
              << " | " << std::setw(8) << "Mine" << "\n";
    std::cout << std::string(85, '-') << "\n";

    for (const auto& a : areas)
    {
        std::cout << std::left << std::setw(20) << a.name << " | "
                  << std::setw(10) << a.pac_width << " | " << std::setw(10)
                  << a.pac_height << " | " << std::setw(8)
                  << a.area_facilities.at("zipline") << " | "
                  << a.area_facilities.at("defense") << " | "
                  << a.area_facilities.at("mining_rig") << "\n";
    }
}
