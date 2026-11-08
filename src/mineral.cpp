#include "mineral.hpp"
#include <iomanip>
#include <iostream>

namespace aic
{

std::ostream &operator<<(std::ostream &os, const Mineral &m)
{
    os << "Mineral: " << std::left << std::setw(20) << m.name << " | Limit: " << m.limit;
    return os;
}

void Mineral::print_table(const std::vector<Mineral> &minerals)
{
    std::cout << "\n--- Minerals Table ---\n";
    std::cout << std::left << std::setw(20) << "Mineral"
              << " | " << std::setw(10) << "Limit"
              << "\n";
    std::cout << std::string(35, '-') << "\n";

    for (const auto &m : minerals)
    {
        std::cout << std::left << std::setw(20) << m.name << " | " << std::setw(10) << m.limit
                  << "\n";
    }
}

} // namespace aic
