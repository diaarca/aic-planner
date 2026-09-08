#include "area.hpp"
#include <csv.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>

std::vector<Area> Area::readJSON(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open areas JSON file: " + filename);
    }
    nlohmann::json j;
    file >> j;

    std::vector<Area> areas;
    for (const auto& item : j)
    {
        Area a;
        a.name = item.value("area", "");
        a.pac_width = item.value("pac_width", 0.0);
        a.pac_height = item.value("pac_height", 0.0);

        double total_depot_w = 0;
        double total_depot_h = 0;
        if (item.contains("static_depots"))
        {
            for (const auto& d : item["static_depots"])
            {
                StaticDepot sd;
                if (d.contains("x1"))
                {
                    sd.x1 = d.value("x1", 0);
                    sd.y1 = d.value("y1", 0);
                    sd.x2 = d.value("x2", 0);
                    sd.y2 = d.value("y2", 0);
                    if (sd.y1 == sd.y2)
                    {
                        total_depot_w += std::abs(sd.x2 - sd.x1) + 1;
                    }
                    else
                    {
                        total_depot_h += std::abs(sd.y2 - sd.y1) + 1;
                    }
                }
                else if (d.contains("x"))
                {
                    sd.x1 = sd.x2 = d.value("x", 0);
                    sd.y1 = sd.y2 = d.value("y", 0);
                    total_depot_w += 1;
                }
                a.static_depots.push_back(sd);
            }
        }
        a.pac_depot_width = total_depot_w;
        a.pac_depot_height = total_depot_h;

        a.area_facilities["zipline"] = item.value("zipline", 0.0);
        a.area_facilities["defense"] = item.value("defense", 0.0);
        a.area_facilities["mining_rig"] = item.value("mining_rig", 0.0);
        areas.push_back(a);
    }
    return areas;
}

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
              << " | " << std::setw(8) << "Mine"
              << "\n";
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
