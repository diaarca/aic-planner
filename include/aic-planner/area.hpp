#pragma once

#include <iostream>
#include <map>
#include <string>
#include <vector>

struct StaticDepot
{
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
};

struct Area
{
    std::string name;
    double pac_width = 0;
    double pac_height = 0;
    double pac_depot_width = 0;
    double pac_depot_height = 0;
    std::map<std::string, double> area_facilities;
    std::vector<StaticDepot> static_depots;

    static std::vector<Area> readJSON(const std::string& filename);
    static std::vector<Area> readCSV(const std::string& filename);

    friend std::ostream& operator<<(std::ostream& os, const Area& a);
    static void print_table(const std::vector<Area>& areas);
};
