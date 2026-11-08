#include "area.hpp"
#include "json_io.hpp"
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>

namespace aic
{

std::vector<Area> Area::read_json(const std::string &filename)
{
    const auto j = read_json_file(filename);

    std::vector<Area> areas;
    for (const auto &item : j)
    {
        Area a;
        a.name = item.value("area", "");
        a.pac_width = item.value("pac_width", 0.0);
        a.pac_height = item.value("pac_height", 0.0);

        double total_depot_w = 0;
        double total_depot_h = 0;
        if (item.contains("static_depots"))
        {
            for (const auto &d : item["static_depots"])
            {
                StaticDepot sd;
                sd.x1 = d.at("x1");
                sd.y1 = d.at("y1");
                sd.x2 = d.at("x2");
                sd.y2 = d.at("y2");
                if (sd.y1 == sd.y2)
                    total_depot_w += std::abs(sd.x2 - sd.x1) + 1;
                else if (sd.x1 == sd.x2)
                    total_depot_h += std::abs(sd.y2 - sd.y1) + 1;
                else
                    throw std::invalid_argument("Depot segments must be horizontal or vertical");
                a.static_depots.push_back(sd);
            }
        }
        a.pac_depot_width = total_depot_w;
        a.pac_depot_height = total_depot_h;

        a.area_facilities = item.value("facilities", std::map<std::string, double>{});
        areas.push_back(a);
    }
    return areas;
}

std::ostream &operator<<(std::ostream &os, const Area &a)
{
    os << "Area: " << std::left << std::setw(20) << a.name << " | Size: " << a.pac_width << "x"
       << a.pac_height;
    return os;
}

void Area::print_table(const std::vector<Area> &areas)
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

    for (const auto &a : areas)
    {
        std::cout << std::left << std::setw(20) << a.name << " | " << std::setw(10) << a.pac_width
                  << " | " << std::setw(10) << a.pac_height << " | " << std::setw(8)
                  << a.area_facilities.at("zipline") << " | " << a.area_facilities.at("defense")
                  << " | " << a.area_facilities.at("mining_rig") << "\n";
    }
}

} // namespace aic
