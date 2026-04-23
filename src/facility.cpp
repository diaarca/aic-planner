#include "facility.hpp"
#include <csv.h>
#include <iomanip>
#include <iostream>

std::map<std::string, double> Facility::readCSV(const std::string& filename)
{
    std::map<std::string, double> results;
    io::CSVReader<2> in(filename);
    in.read_header(io::ignore_extra_column, "facility", "power");
    std::string name;
    double power;
    while (in.read_row(name, power))
    {
        results[name] = power;
    }
    return results;
}

std::ostream& operator<<(std::ostream& os, const Facility& f)
{
    os << "Facility: " << std::left << std::setw(20) << f.name
       << " | Power: " << f.power;
    return os;
}

void Facility::print_table(const std::map<std::string, double>& facilities)
{
    std::cout << "\n--- Facilities Table ---\n";
    std::cout << std::left << std::setw(25) << "Facility"
              << " | " << std::setw(10) << "Power" << "\n";
    std::cout << std::string(40, '-') << "\n";

    for (const auto& [name, power] : facilities)
    {
        std::cout << std::left << std::setw(25) << name << " | "
                  << std::setw(10) << power << "\n";
    }
}
