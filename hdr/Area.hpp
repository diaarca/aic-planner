#pragma once

#include <string>
#include <map>

struct Area
{
    std::string name;
    double pac_width;
    double pac_height;
    double pac_depot;
    std::map<std::string, double> area_facilities;
};
