#pragma once

#include <string>
#include <map>

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

    // Fuel related information
    bool is_fuel = false;
    double fuel_power = 0;
    double fuel_duration = 0;
};
