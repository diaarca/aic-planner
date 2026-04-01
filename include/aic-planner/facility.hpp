#pragma once

#include "csv_reader.hpp"
#include <map>
#include <string>
#include <vector>

struct Facility
{
    std::string name;
    double power;

    static std::map<std::string, double> readCSV(const std::string& filename);
};
