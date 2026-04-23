#include "mineral.hpp"
#include <csv.h>
#include <iomanip>
#include <iostream>

std::vector<Mineral> Mineral::readCSV(const std::string& filename)
{
    std::vector<Mineral> minerals;
    io::CSVReader<2> in(filename);
    in.read_header(io::ignore_extra_column, "mineral", "limit");
    std::string name;
    double limit;
    while (in.read_row(name, limit))
    {
        minerals.emplace_back(name, limit);
    }
    return minerals;
}

std::ostream& operator<<(std::ostream& os, const Mineral& m)
{
    os << "Mineral: " << std::left << std::setw(20) << m.name
       << " | Limit: " << m.limit;
    return os;
}

void Mineral::print_table(const std::vector<Mineral>& minerals)
{
    std::cout << "\n--- Minerals Table ---\n";
    std::cout << std::left << std::setw(20) << "Mineral"
              << " | " << std::setw(10) << "Limit" << "\n";
    std::cout << std::string(35, '-') << "\n";

    for (const auto& m : minerals)
    {
        std::cout << std::left << std::setw(20) << m.name << " | "
                  << std::setw(10) << m.limit << "\n";
    }
}
