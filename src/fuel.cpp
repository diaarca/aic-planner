#include "fuel.hpp"
#include <csv.h>
#include <iomanip>
#include <iostream>

std::vector<Fuel> Fuel::readCSV(const std::string& filename)
{
    std::vector<Fuel> fuels;
    io::CSVReader<3> in(filename);
    in.read_header(io::ignore_extra_column, "fuel", "power", "duration");
    std::string name;
    double power, duration;
    while (in.read_row(name, power, duration))
    {
        fuels.emplace_back(name, power, duration);
    }
    return fuels;
}

std::ostream& operator<<(std::ostream& os, const Fuel& f)
{
    os << "Fuel: " << std::left << std::setw(20) << f.name
       << " | Power: " << std::setw(10) << f.power
       << " | Duration: " << f.duration;
    return os;
}

void Fuel::print_table(const std::vector<Fuel>& fuels)
{
    std::cout << "\n--- Fuels Table ---\n";
    std::cout << std::left << std::setw(20) << "Fuel"
              << " | " << std::setw(10) << "Power"
              << " | " << std::setw(10) << "Duration" << "\n";
    std::cout << std::string(50, '-') << "\n";

    for (const auto& f : fuels)
    {
        std::cout << std::left << std::setw(20) << f.name << " | "
                  << std::setw(10) << f.power << " | " << std::setw(10)
                  << f.duration << "\n";
    }
}
