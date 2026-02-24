#pragma once

#include "Area.hpp"
#include "Product.hpp"
#include <map>
#include <string>
#include <vector>

namespace DataReader
{

std::map<std::string, double> read_minerals(const std::string& filename);
std::vector<Product> read_products(const std::string& filename);
std::vector<Area> read_areas(const std::string& filename);
void update_products_with_factories(std::vector<Product>& products,
                                    const std::string& filename);
std::map<std::string, double> read_facilities(const std::string& filename);
void update_products_with_fuels(std::vector<Product>& products,
                                const std::string& filename);

} // namespace DataReader
