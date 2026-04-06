#include "area.hpp"
#include <csv.h>
#include <iostream>

std::vector<Area> Area::readCSV(const std::string& filename)
{
    std::vector<Area> areas;
    try
    {
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
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading areas from " << filename << ": " << e.what()
                  << std::endl;
    }
    return areas;
}
