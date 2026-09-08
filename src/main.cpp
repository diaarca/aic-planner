#include "area.hpp"
#include "blueprint_solver.hpp"
#include "facility.hpp"
#include "fuel.hpp"
#include "mineral.hpp"
#include "product.hpp"
#include "region.hpp"
#include "solver.hpp"
#include <filesystem>
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
    bool force_csv = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--csv")
        {
            force_csv = true;
        }
    }

    try
    {
        std::string json_region = folder_path + "/region.json";
        bool is_json = !force_csv && std::filesystem::exists(json_region);

        if (is_json)
        {
            std::cout << "Detected JSON dataset in: " << folder_path
                      << std::endl;
            Region region = Region::readJSON(folder_path + "/region.json");
            std::vector<Mineral> minerals =
                Mineral::readJSON(folder_path + "/minerals.json");
            std::vector<Product> products =
                Product::readJSON(folder_path + "/products.json");
            std::vector<Area> areas =
                Area::readJSON(folder_path + "/areas.json");
            std::vector<Fuel> fuels =
                Fuel::readJSON(folder_path + "/fuels.json");
            std::vector<Facility> facilities =
                Facility::readJSON(folder_path + "/facilities.json");

            std::map<std::string, double> facility_powers;
            for (const auto& f : facilities)
            {
                facility_powers[f.name] = f.power;
            }

            // STAGE 1: Blueprint Model (Subproblem)
            std::cout << "\n==================================================="
                         "====\n";
            std::cout << "  STAGE 1: BLUEPRINT OPTIMIZATION (Subproblem)\n";
            std::cout
                << "=======================================================\n";
            BlueprintSolver blueprint_solver(facilities, minerals, areas);
            std::vector<Product> producible_products;

            for (auto& prod : products)
            {
                BlueprintLayout layout;
                if (blueprint_solver.solve(prod, layout))
                {
                    std::cout << "\n[OK] Blueprint generated for: " << prod.name
                              << "\n";
                    layout.print();
                    producible_products.push_back(prod);
                }
                else
                {
                    std::cout << "[INFO] Product '" << prod.name
                              << "' not producible with current facility "
                                 "recipes. Skipping.\n";
                }
            }

            // STAGE 2: Master Solver
            std::cout << "\n==================================================="
                         "====\n";
            std::cout << "  STAGE 2: REGIONAL MASTER SOLVER\n";
            std::cout
                << "=======================================================\n";
            if (producible_products.empty())
            {
                std::cerr << "No producible products available for regional "
                             "master solver."
                          << std::endl;
                return 1;
            }

            Solver solver(producible_products, minerals, areas, fuels,
                          facility_powers, region);
            solver.solve();
        }
        else
        {
            // Legacy CSV flow
            std::string region_file = folder_path + "/region.csv";
            std::string minerals_file = folder_path + "/minerals.csv";
            std::string products_file = folder_path + "/products.csv";
            std::string areas_file = folder_path + "/areas.csv";
            std::string fuels_file = folder_path + "/fuels.csv";
            std::string facilities_file = folder_path + "/facilities.csv";

            Region region = Region::readCSV(region_file);
            std::vector<Mineral> minerals = Mineral::readCSV(minerals_file);
            std::vector<Product> products = Product::readCSV(products_file);
            std::vector<Area> areas = Area::readCSV(areas_file);
            std::vector<Fuel> fuels = Fuel::readCSV(fuels_file);
            std::map<std::string, double> facilities =
                Facility::readCSV(facilities_file);

            if (minerals.empty() || products.empty() || areas.empty() ||
                fuels.empty() || facilities.empty())
            {
                throw std::runtime_error("Essential data (products or "
                                         "minerals) is missing or empty.");
            }

            Solver solver(products, minerals, areas, fuels, facilities, region);
            solver.solve();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nFATAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
