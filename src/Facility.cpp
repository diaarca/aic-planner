#include "facility.hpp"
#include <csv.h>
#include <iostream>

std::map<std::string, double> Facility::readCSV(const std::string& filename)
{
    std::map<std::string, double> results;
    try
    {
        io::CSVReader<2> in(filename);
        in.read_header(io::ignore_extra_column, "facility", "power");
        std::string name;
        double power;
        while (in.read_row(name, power))
        {
            results[name] = power;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading facilities from " << filename << ": "
                  << e.what() << std::endl;
    }
    return results;
}
