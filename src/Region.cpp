#include "region.hpp"
#include <csv.h>
#include <iostream>

Region Region::readCSV(const std::string& filename)
{
    Region region;
    try
    {
        io::CSVReader<2> in(filename);
        in.read_header(io::ignore_extra_column, "information", "value");
        std::string info;
        double value;
        while (in.read_row(info, value))
        {
            if (info == "base_power")
            {
                region.base_power = value;
            }
            else if (info == "storage")
            {
                region.storage = value;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error reading region from " << filename << ": "
                  << e.what() << std::endl;
    }
    return region;
}
