#include "area.hpp"
#include "mineral.hpp"
#include "solver.hpp"
#include "fuel.hpp"
#include "facility.hpp"
#include "region.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
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
        // Read data
        std::string minerals_file = folder_path + "/minerals.csv";
        std::string products_file = folder_path + "/products.csv";
        std::string areas_file = folder_path + "/areas.csv";
        std::string fuels_file = folder_path + "/fuels.csv";
        std::string facilities_file = folder_path + "/facilities.csv";
        std::string region_file = folder_path + "/region.csv";
        {
            std::ifstream f_test(fuels_file);
            if (!f_test.good())
            {
                fuels_file = folder_path + "/batteries.csv";
            }
        }
        auto mineral_limits = Mineral::readCSV(minerals_file);
        auto products = Product::readCSV(products_file, mineral_limits);
        auto areas = Area::readCSV(areas_file);
        auto fuels = Fuel::readCSV(fuels_file);
        auto facility_power = Facility::readCSV(facilities_file);
        auto region = Region::readCSV(region_file);

        if (products.empty() || mineral_limits.empty())
        {
            std::cerr << "Data files are empty or could not be read properly."
                      << std::endl;
            return 1;
        }

        // --- Toggle Display of Input Data Tables ---
        // Mineral::print_table(mineral_limits);
        // Product::print_table(products, mineral_limits);
        // Area::print_table(areas);

        // Create solver and solve the model
        Solver solver(products, mineral_limits, areas, fuels, facility_power, region);
        solver.solve();
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nAn error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
