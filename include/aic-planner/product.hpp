#pragma once

#include "mineral.hpp"
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Product
{
    std::string name;
    double value;
    std::map<std::string, double> mineral_consumption;
    double production_time;

    double factory_width;
    double factory_height;
    double factory_depot;
    std::map<std::string, double> factory_facilities;

    static std::vector<Product> readCSV(const std::string& filename, const std::vector<Mineral>& mineral_limits);

    friend std::ostream& operator<<(std::ostream& os, const Product& p);
    static void print_table(const std::vector<Product>& products);
};
