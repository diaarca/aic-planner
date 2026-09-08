#include "product.hpp"
#include <csv.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

std::vector<Product> Product::readJSON(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open products JSON file: " +
                                 filename);
    }
    nlohmann::json j;
    file >> j;

    std::vector<Product> products;
    for (const auto& item : j)
    {
        Product prod;
        prod.name = item.value("product", "");
        prod.value = item.value("value", 0.0);
        products.push_back(prod);
    }
    return products;
}

std::vector<Product> Product::readCSV(const std::string& filename)
{
    std::vector<Product> products;
    io::CSVReader<22> in(filename);
    in.read_header(
        io::ignore_extra_column, "product", "value", "time", "originium",
        "amethyst", "ferrium", "depot", "width", "height", "zipline",
        "mining_rig", "protocol_stash", "refining_unit", "shredding_unit",
        "moulding_unit", "seed_picking_unit", "gearing_unit", "fitting_unit",
        "planting_unit", "filling_unit", "packaging_unit", "grinding_unit");

    std::string name;
    double value, time, originium, amethyst, ferrium, depot, width, height;
    double zipline, mining_rig, protocol_stash, refining_unit, shredding_unit,
        moulding_unit, seed_picking_unit, gearing_unit, fitting_unit,
        planting_unit, filling_unit, packaging_unit, grinding_unit;

    while (in.read_row(
        name, value, time, originium, amethyst, ferrium, depot, width, height,
        zipline, mining_rig, protocol_stash, refining_unit, shredding_unit,
        moulding_unit, seed_picking_unit, gearing_unit, fitting_unit,
        planting_unit, filling_unit, packaging_unit, grinding_unit))
    {
        Product prod;
        prod.name = name;
        prod.value = value;
        prod.production_time = time;
        prod.mineral_consumption["originium"] = originium;
        prod.mineral_consumption["amethyst"] = amethyst;
        prod.mineral_consumption["ferrium"] = ferrium;
        prod.factory_depot = depot;
        prod.factory_width = width;
        prod.factory_height = height;

        prod.factory_facilities["zipline"] = zipline;
        prod.factory_facilities["mining_rig"] = mining_rig;
        prod.factory_facilities["protocol_stash"] = protocol_stash;
        prod.factory_facilities["refining_unit"] = refining_unit;
        prod.factory_facilities["shredding_unit"] = shredding_unit;
        prod.factory_facilities["moulding_unit"] = moulding_unit;
        prod.factory_facilities["seed_picking_unit"] = seed_picking_unit;
        prod.factory_facilities["gearing_unit"] = gearing_unit;
        prod.factory_facilities["fitting_unit"] = fitting_unit;
        prod.factory_facilities["planting_unit"] = planting_unit;
        prod.factory_facilities["filling_unit"] = filling_unit;
        prod.factory_facilities["packaging_unit"] = packaging_unit;
        prod.factory_facilities["grinding_unit"] = grinding_unit;

        products.push_back(prod);
    }
    return products;
}

std::ostream& operator<<(std::ostream& os, const Product& p)
{
    os << "Product: " << std::left << std::setw(20) << p.name
       << " | Value: " << p.value;
    return os;
}

void Product::print_table(const std::vector<Product>& products)
{
    std::cout << "\n--- Products Table ---\n";
    std::cout << std::left << std::setw(20) << "Product"
              << " | " << std::setw(6) << "Val"
              << " | " << std::setw(6) << "Time"
              << " | " << std::setw(4) << "Ori"
              << " | " << std::setw(4) << "Ame"
              << " | " << std::setw(4) << "Fer"
              << " | " << std::setw(6) << "W"
              << "x" << std::setw(6) << "H"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& p : products)
    {
        std::cout << std::left << std::setw(20) << p.name << " | "
                  << std::setw(6) << p.value << " | " << std::setw(6)
                  << p.production_time << " | " << std::setw(4)
                  << p.mineral_consumption.at("originium") << " | "
                  << p.mineral_consumption.at("amethyst") << " | "
                  << p.mineral_consumption.at("ferrium") << " | "
                  << std::setw(6) << p.factory_width << "x" << std::setw(6)
                  << p.factory_height << "\n";
    }
}
