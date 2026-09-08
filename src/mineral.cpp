#include "mineral.hpp"
#include <csv.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

std::vector<Mineral> Mineral::readJSON(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open minerals JSON file: " +
                                 filename);
    }
    nlohmann::json j;
    file >> j;

    std::vector<Mineral> minerals;
    for (const auto& item : j)
    {
        minerals.emplace_back(item.value("mineral", ""),
                              item.value("limit", 0.0));
    }
    return minerals;
}

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
              << " | " << std::setw(10) << "Limit"
              << "\n";
    std::cout << std::string(35, '-') << "\n";

    for (const auto& m : minerals)
    {
        std::cout << std::left << std::setw(20) << m.name << " | "
                  << std::setw(10) << m.limit << "\n";
    }
}
