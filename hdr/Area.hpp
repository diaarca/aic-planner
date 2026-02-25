#pragma once

#include <string>
#include <map>
#include <vector>
#include "CSVReader.hpp"

struct Area
{
    std::string name;
    double pac_width;
    double pac_height;
    double pac_depot;
    std::map<std::string, double> area_facilities;

    static std::vector<Area> readCSV(const std::string& filename);
    static void print_table(const std::vector<Area>& areas);
};
