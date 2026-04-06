#include "mineral.hpp"
#include <csv.h>
#include <iostream>

std::vector<Mineral> Mineral::readCSV(const std::string& filename)
{
    std::vector<Mineral> minerals;
    try
    {
        io::CSVReader<2> in(filename);
        in.read_header(io::ignore_extra_column, "mineral", "limit");
        std::string name;
        double limit;
        while (in.read_row(name, limit))
        {
            minerals.emplace_back(name, limit);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading minerals from " << filename << ": "
                  << e.what() << std::endl;
    }
    return minerals;
}
