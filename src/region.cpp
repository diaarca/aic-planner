#include "region.hpp"
#include <csv.h>
#include <iomanip>
#include <iostream>

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
