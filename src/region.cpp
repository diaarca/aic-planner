#include "region.hpp"
#include "json_io.hpp"
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace aic
{

Region Region::read_json(const std::string &filename)
{
    const auto j = read_json_file(filename);

    Region region;
    region.catalog = j.value("catalog", "");
    region.market_region = j.value("market_region", "");
    region.currency = j.value("currency", "");
    region.automatic_recipes = j.value("automatic_recipes", false);
    region.settlement_levels = j.value("settlement_levels", std::map<std::string, int>{});
    region.active_activities = j.value("active_activities", std::vector<std::string>{});
    region.active_time_ids = j.value("active_time_ids", std::vector<std::string>{});
    region.enabled_facilities = j.value("enabled_facilities", std::vector<std::string>{});
    region.enabled_recipes = j.value("enabled_recipes", std::vector<std::string>{});
    for (const auto &resource : j.at("resources"))
        region.resources.emplace_back(resource.at("item").get<std::string>(),
                                      resource.at("limit").get<double>());
    region.base_power = j.value("base_power", 0.0);
    region.storage = j.value("storage", 0.0);
    return region;
}

std::ostream &operator<<(std::ostream &os, const Region &r)
{
    os << "Region: Base Power: " << r.base_power << " | Storage: " << r.storage;
    return os;
}

void Region::print_table(const Region &r)
{
    std::cout << "\n--- Region Info ---\n";
    std::cout << std::left << std::setw(20) << "Base Power"
              << " | " << r.base_power << "\n";
    std::cout << std::left << std::setw(20) << "Storage"
              << " | " << r.storage << "\n";
}

} // namespace aic
