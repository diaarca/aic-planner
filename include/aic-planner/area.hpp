#pragma once

#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Area
{
    std::string name;
    double pac_width;
    double pac_height;
    double pac_depot_width;
    double pac_depot_height;
    std::map<std::string, double> area_facilities;

    static std::vector<Area> readCSV(const std::string& filename);

    friend std::ostream& operator<<(std::ostream& os, const Area& a);
    static void print_table(const std::vector<Area>& areas);
};
