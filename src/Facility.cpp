#include "facility.hpp"
#include <iostream>

std::map<std::string, double> Facility::readCSV(const std::string& filename)
{
    auto f_map = CSVObject::read_csv_map<Facility>(filename, {"facility", "power"});
    std::map<std::string, double> results;
    for (auto const& [name, f] : f_map)
    {
        results[name] = f.power;
    }
    return results;
}

void Facility::load(const std::map<std::string, std::string>& row_data)
{
    name = row_data.at("facility");
    power = std::stod(row_data.at("power"));
}

std::vector<std::string> Facility::get_headers() const
{
    return {"facility", "power"};
}


std::vector<std::string> Facility::get_values() const
{
    return {name, std::to_string(power)};
}
