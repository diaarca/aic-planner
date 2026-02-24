#include "DataReader.hpp"
#include "Solver.hpp"
#include "Area.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
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
        std::string factories_file = folder_path + "/factories.csv";
        std::string facilities_file = folder_path + "/facilities.csv";
        std::string fuels_file = folder_path + "/fuels.csv";

        auto mineral_limits = DataReader::read_minerals(minerals_file);
        auto products = DataReader::read_products(products_file);
        auto areas = DataReader::read_areas(areas_file);
        DataReader::update_products_with_factories(products, factories_file);
        auto facilities_power = DataReader::read_facilities(facilities_file);
        
        // Update products with fuel info and identify mineral fuels
        DataReader::update_products_with_fuels(products, fuels_file);
        
        // Prepare mineral fuels map
        std::map<std::string, std::pair<double, double>> mineral_fuels;
        // We need to read fuels.csv again or use a helper to get mineral fuels
        // Since DataReader::update_products_with_fuels only updates products, 
        // let's manually check for mineral fuels or improve DataReader.
        // For now, let's just do it here for simplicity as we know originium is one.
        // A better way would be a generic read_fuels function.
        
        // Let's improve DataReader to avoid duplicate parsing if possible, 
        // but for now I'll just use a simple approach.
        
        // Re-parsing fuels for minerals
        {
            std::ifstream file(fuels_file);
            std::string line;
            std::getline(file, line); // skip header
            while (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string name, p_str, d_str;
                std::getline(ss, name, ',');
                std::getline(ss, p_str, ',');
                std::getline(ss, d_str, ',');
                if (mineral_limits.count(name)) {
                    mineral_fuels[name] = {std::stod(p_str), std::stod(d_str)};
                }
            }
        }

        if (products.empty() || mineral_limits.empty())
        {
            std::cerr << "Data files are empty or could not be read properly."
                      << std::endl;
            return 1;
        }

        // Create solver and solve the model
        Solver solver(products, mineral_limits, areas, facilities_power, mineral_fuels);
        solver.solve();
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nAn error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
