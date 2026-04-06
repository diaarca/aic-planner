#include "fuel.hpp"
#include <csv.h>
#include <iostream>

std::vector<Fuel> Fuel::readCSV(const std::string& filename)
{
    std::vector<Fuel> fuels;
    try {
        io::CSVReader<3> in(filename);
        in.read_header(io::ignore_extra_column, "fuel", "power", "duration");
        std::string name;
        double power, duration;
        while(in.read_row(name, power, duration)){
            fuels.emplace_back(name, power, duration);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading fuels from " << filename << ": " << e.what() << std::endl;
    }
    return fuels;
}
