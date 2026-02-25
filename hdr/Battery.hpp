#pragma once

#include <string>
#include <vector>
#include "CSVReader.hpp"

struct Battery
{
    std::string name;
    double power;
    double duration;

    static std::vector<Battery> readCSV(const std::string& filename);
};
