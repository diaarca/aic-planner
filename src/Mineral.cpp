#include "mineral.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

std::vector<Mineral> Mineral::readCSV(const std::string& filename)
{
    return CSVObject::read_csv_vector<Mineral>(filename, {"mineral", "limit"});
}

void Mineral::load(const std::map<std::string, std::string>& row_data)
{
    name = row_data.at("mineral");
    limit = std::stod(row_data.at("limit"));
}

std::vector<std::string> Mineral::get_headers() const
{
    return {"mineral", "limit"};
}


std::vector<std::string> Mineral::get_values() const
{
    return {name, std::to_string(limit)};
}
