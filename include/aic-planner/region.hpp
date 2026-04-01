#pragma once

#include <string>
#include <map>

struct Region
{
    double base_power = 0;
    double storage = 0;

    static Region readCSV(const std::string& filename);
};
