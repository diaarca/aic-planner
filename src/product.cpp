#include "product.hpp"
#include "json_io.hpp"
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace aic
{

std::vector<Product> Product::read_json(const std::string &filename)
{
    const auto j = read_json_file(filename);

    std::vector<Product> products;
    for (const auto &item : j)
    {
        Product prod;
        prod.name = item.value("product", "");
        prod.value = item.value("value", 0.0);
        products.push_back(prod);
    }
    return products;
}

std::ostream &operator<<(std::ostream &os, const Product &p)
{
    os << "Product: " << std::left << std::setw(20) << p.name << " | Value: " << p.value;
    return os;
}

void Product::print_table(const std::vector<Product> &products)
{
    std::cout << "Product | Value | Seconds/unit | Footprint\n";
    for (const auto &product : products)
        std::cout << product.name << " | " << product.value << " | " << product.production_time
                  << " | " << product.factory_width << 'x' << product.factory_height << '\n';
}

} // namespace aic
