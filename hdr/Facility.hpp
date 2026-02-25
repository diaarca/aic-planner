#pragma once

#include <string>
#include <map>
#include <vector>
#include "CSVReader.hpp"

struct Facility
{
    std::string name;
    double power;

    static std::map<std::string, double> readCSV(const std::string& filename);
};
