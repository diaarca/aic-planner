#include "product.hpp"
#include <csv.h>
#include <iostream>

std::vector<Product> Product::readCSV(const std::string& filename, const std::vector<Mineral>& mineral_limits)
{
    std::vector<Product> products;
    try {
        io::CSVReader<22> in(filename);
        in.read_header(io::ignore_extra_column,
            "product", "value", "time", "originium", "amethyst", "ferrium", "depot", "width", "height",
            "zipline", "mining_rig", "protocol_stash", "refining_unit", "shredding_unit", "moulding_unit",
            "seed_picking_unit", "gearing_unit", "fitting_unit", "planting_unit", "filling_unit", "packaging_unit", "grinding_unit");
        
        std::string name;
        double value, time, originium, amethyst, ferrium, depot, width, height;
        double zipline, mining_rig, protocol_stash, refining_unit, shredding_unit, moulding_unit,
               seed_picking_unit, gearing_unit, fitting_unit, planting_unit, filling_unit, packaging_unit, grinding_unit;
        
        while(in.read_row(name, value, time, originium, amethyst, ferrium, depot, width, height,
                        zipline, mining_rig, protocol_stash, refining_unit, shredding_unit, moulding_unit,
                        seed_picking_unit, gearing_unit, fitting_unit, planting_unit, filling_unit, packaging_unit, grinding_unit)){
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
    } catch (const std::exception& e) {
        std::cerr << "Error reading products from " << filename << ": " << e.what() << std::endl;
    }
    return products;
}
