#include "mineral.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

std::vector<Mineral> Mineral::readCSV(const std::string& filename)
{
    std::vector<Mineral> minerals;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return minerals;
    }
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        auto row = CSVReader::parse_line(line);
        if (row.size() >= 2)
        {
            minerals.push_back({row[0], std::stod(row[1])});
        }
    }
    return minerals;
}

void Mineral::print_table(const std::vector<Mineral>& minerals)
{
    if (minerals.empty())
        return;
    std::cout << "\n--- Minerals ---\n";
    std::cout << std::left << std::setw(15) << "Mineral" << " | "
              << std::setw(8) << "Limit" << "\n";
    std::cout << std::string(26, '-') << "\n";
    for (const auto& mineral : minerals)
    {
        std::cout << std::left << std::setw(15) << mineral.name << " | "
                  << std::setw(8) << mineral.limit << "\n";
    }
}
