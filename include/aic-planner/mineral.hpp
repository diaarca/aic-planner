#pragma once

#include "csv_reader.hpp"
#include <map>
#include <string>
#include <vector>

struct Mineral
{
    std::string name;
    double limit;

    static std::vector<Mineral> readCSV(const std::string& filename);
    static void print_table(const std::vector<Mineral>& minerals);
};
