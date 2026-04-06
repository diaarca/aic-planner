#pragma once

#include <string>
#include <vector>

struct Mineral
{
    std::string name;
    double limit;

    Mineral() = default;
    Mineral(std::string n, double l) : name(n), limit(l) {}

    static std::vector<Mineral> readCSV(const std::string& filename);
};
