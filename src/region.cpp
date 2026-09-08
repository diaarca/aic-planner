#include "region.hpp"
#include <csv.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

Region Region::readJSON(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open region JSON file: " +
                                 filename);
    }
    nlohmann::json j;
    file >> j;

    Region region;
    region.base_power = j.value("base_power", 0.0);
    region.storage = j.value("storage", 0.0);
    return region;
}

Region Region::readCSV(const std::string& filename)
{
    Region region;
    io::CSVReader<2> in(filename);
    in.read_header(io::ignore_extra_column, "information", "value");
    std::string info;
    double value;
    while (in.read_row(info, value))
    {
        if (info == "base_power")
        {
            region.base_power = value;
        }
        else if (info == "storage")
        {
            region.storage = value;
        }
    }
    return region;
}

std::ostream& operator<<(std::ostream& os, const Region& r)
{
    os << "Region: Base Power: " << r.base_power << " | Storage: " << r.storage;
    return os;
}

void Region::print_table(const Region& r)
{
    std::cout << "\n--- Region Info ---\n";
    std::cout << std::left << std::setw(20) << "Base Power"
              << " | " << r.base_power << "\n";
    std::cout << std::left << std::setw(20) << "Storage"
              << " | " << r.storage << "\n";
}
