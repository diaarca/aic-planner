#include "area.hpp"
#include "facility.hpp"
#include "fuel.hpp"
#include "mineral.hpp"
#include "region.hpp"
#include "solver.hpp"
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <data_folder_path>" << std::endl;
        return 1;
    }
    std::string folder_path = argv[1];

    try
    {
        // read data
        std::string region_file = folder_path + "/region.csv";
        std::string minerals_file = folder_path + "/minerals.csv";
        std::string products_file = folder_path + "/products.csv";
        std::string areas_file = folder_path + "/areas.csv";
        std::string fuels_file = folder_path + "/fuels.csv";
        std::string facilities_file = folder_path + "/facilities.csv";

        Region region = Region::readCSV(region_file);
        std::vector<Mineral> minerals = Mineral::readCSV(minerals_file);
        std::vector<Product> products =
            Product::readCSV(products_file, minerals);
        std::vector<Area> areas = Area::readCSV(areas_file);
        std::vector<Fuel> fuels = Fuel::readCSV(fuels_file);
        std::map<std::string, double> facilities =
            Facility::readCSV(facilities_file);

        if (minerals.empty() || products.empty() || areas.empty() ||
            fuels.empty() || facilities.empty())
        {
            throw std::runtime_error(
                "Essential data (products or minerals) is missing or empty.");
        }

        // instanciate and solve the model
        Solver solver(products, minerals, areas, fuels, facilities, region);
        solver.solve();
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nFATAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
