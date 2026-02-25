#pragma once

#include <map>
#include <string>
#include <vector>
#include "CSVReader.hpp"

// Data structure for a product
struct Product
{
    std::string name;
    double value;
    std::map<std::string, double> mineral_consumption;
    double production_time; // Time in seconds to produce one unit

    // Factory related information
    double factory_width;
    double factory_height;
    double factory_depot;
    std::map<std::string, double> factory_facilities;

    static std::vector<Product> readCSV(const std::string& filename,
                                       const std::map<std::string, double>& mineral_limits);
    static void print_table(const std::vector<Product>& products,
                            const std::map<std::string, double>& mineral_limits);
};
