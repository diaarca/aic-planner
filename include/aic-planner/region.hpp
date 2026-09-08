#pragma once

#include <iostream>
#include <string>
#include <vector>

struct Region
{
    double base_power = 0;
    double storage = 0;

    static Region readJSON(const std::string& filename);
    static Region readCSV(const std::string& filename);

    friend std::ostream& operator<<(std::ostream& os, const Region& r);
    static void print_table(const Region& r);
};
