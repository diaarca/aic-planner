#include "Mineral.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

std::map<std::string, double> Mineral::readCSV(const std::string& filename)
{
    std::map<std::string, double> minerals;
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
            minerals[row[0]] = std::stod(row[1]);
        }
    }
    return minerals;
}

void Mineral::print_table(const std::map<std::string, double>& minerals)
{
    if (minerals.empty())
        return;
    std::cout << "\n--- Minerals ---\n";
    std::cout << std::left << std::setw(15) << "Mineral" << " | "
              << std::setw(8) << "Limit" << "\n";
    std::cout << std::string(26, '-') << "\n";
    for (const auto& pair : minerals)
    {
        std::cout << std::left << std::setw(15) << pair.first << " | "
                  << std::setw(8) << pair.second << "\n";
    }
}
