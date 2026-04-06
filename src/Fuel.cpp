#include "fuel.hpp"
#include <iostream>

std::vector<Fuel> Fuel::readCSV(const std::string& filename)
{
    return CSVObject::read_csv_vector<Fuel>(filename, {"fuel", "power", "duration"});
}

void Fuel::load(const std::map<std::string, std::string>& row_data)
{
    name = row_data.at("fuel");
    power = std::stod(row_data.at("power"));
    duration = std::stod(row_data.at("duration"));
}

std::vector<std::string> Fuel::get_headers() const
{
    return {"fuel", "power", "duration"};
}


std::vector<std::string> Fuel::get_values() const
{
    return {name, std::to_string(power), std::to_string(duration)};
}
