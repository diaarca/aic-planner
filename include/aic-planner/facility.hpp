#pragma once

#include <map>
#include <string>

struct Facility
{
    std::string name;
    double power;

    static std::map<std::string, double> readCSV(const std::string& filename);
};
