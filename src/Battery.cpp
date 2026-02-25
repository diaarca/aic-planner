#include "Battery.hpp"
#include <fstream>

std::vector<Battery> Battery::readCSV(const std::string& filename)
{
    std::vector<Battery> batteries;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return batteries;
    }
    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line))
    {
        auto row = CSVReader::parse_line(line);
        if (row.size() >= 3)
        {
            Battery b;
            b.name = row[0];
            b.power = std::stod(row[1]);
            b.duration = std::stod(row[2]);
            batteries.push_back(b);
        }
    }
    return batteries;
}
