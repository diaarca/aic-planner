#pragma once

#include <string>
#include <vector>
#include "csv_reader.hpp"

struct Fuel
{
    std::string name;
    double power;
    double duration;

    static std::vector<Fuel> readCSV(const std::string& filename);
};
