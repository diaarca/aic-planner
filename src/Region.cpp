#include "region.hpp"
#include "csv_reader.hpp"
#include <fstream>
#include <iostream>
#include <vector>

Region Region::readCSV(const std::string& filename)
{
    Region region;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        return region;
    }
    std::string line;
    // Skip header
    if (!std::getline(file, line))
        return region;

    while (std::getline(file, line))
    {
        auto row = CSVReader::parse_line(line);
        if (row.size() < 2)
            continue;

        if (row[0] == "base_power")
        {
            region.base_power = std::stod(row[1]);
        }
        else if (row[0] == "storage")
        {
            region.storage = std::stod(row[1]);
        }
    }
    return region;
}
