#pragma once

#include <string>
#include <vector>
#include "CSVReader.hpp"

struct Fuel
{
    std::string name;
    double power;
    double duration;

    static std::vector<Fuel> readCSV(const std::string& filename);
};
