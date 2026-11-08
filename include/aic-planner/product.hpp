#pragma once

#include "mineral.hpp"
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace aic
{

struct Product
{
    std::string name;
    double value = 0;
    std::map<std::string, double> mineral_consumption;
    double production_time = 0;

    double factory_width = 0;
    double factory_height = 0;
    double factory_depot = 0;
    std::map<std::string, double> factory_facilities;

    static std::vector<Product> read_json(const std::string &filename);

    friend std::ostream &operator<<(std::ostream &os, const Product &p);
    static void print_table(const std::vector<Product> &products);
};

} // namespace aic
