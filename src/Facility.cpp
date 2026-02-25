#include "Facility.hpp"
#include <fstream>

std::map<std::string, double> Facility::readCSV(const std::string& filename)
{
    std::map<std::string, double> facilities;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return facilities;
    }
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        auto row = CSVReader::parse_line(line);
        if (row.size() >= 2)
        {
            facilities[row[0]] = std::stod(row[1]);
        }
    }
    return facilities;
}
