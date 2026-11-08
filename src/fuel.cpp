#include "fuel.hpp"
#include "json_io.hpp"
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace aic
{

std::vector<Fuel> Fuel::read_json(const std::string &filename)
{
    const auto j = read_json_file(filename);

    std::vector<Fuel> fuels;
    for (const auto &item : j)
    {
        fuels.emplace_back(item.value("fuel", ""), item.value("power", 0.0),
                           item.value("duration", 0.0));
    }
    return fuels;
}

std::ostream &operator<<(std::ostream &os, const Fuel &f)
{
    os << "Fuel: " << std::left << std::setw(20) << f.name << " | Power: " << std::setw(10)
       << f.power << " | Duration: " << f.duration;
    return os;
}

void Fuel::print_table(const std::vector<Fuel> &fuels)
{
    std::cout << "\n--- Fuels Table ---\n";
    std::cout << std::left << std::setw(20) << "Fuel"
              << " | " << std::setw(10) << "Power"
              << " | " << std::setw(10) << "Duration"
              << "\n";
    std::cout << std::string(50, '-') << "\n";

    for (const auto &f : fuels)
    {
        std::cout << std::left << std::setw(20) << f.name << " | " << std::setw(10) << f.power
                  << " | " << std::setw(10) << f.duration << "\n";
    }
}

} // namespace aic
