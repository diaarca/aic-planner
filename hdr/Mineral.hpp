#pragma once

#include <string>
#include <map>
#include <vector>
#include "CSVReader.hpp"

struct Mineral
{
    std::string name;
    double limit;

    static std::map<std::string, double> readCSV(const std::string& filename);
    static void print_table(const std::map<std::string, double>& minerals);
};
