#include "Fuel.hpp"
#include <fstream>

std::vector<Fuel> Fuel::readCSV(const std::string& filename)
{
    std::vector<Fuel> fuels;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return fuels;
    }
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        auto row = CSVReader::parse_line(line);
        if (row.size() >= 3)
        {
            Fuel f;
            f.name = row[0];
            f.power = std::stod(row[1]);
            f.duration = std::stod(row[2]);
            fuels.push_back(f);
        }
    }
    return fuels;
}
