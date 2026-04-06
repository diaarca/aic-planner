#pragma once

#include <string>
#include <vector>

struct Fuel
{
    std::string name;
    double power;
    double duration;

    Fuel() = default;
    Fuel(std::string n, double p, double d) : name(n), power(p), duration(d) {}

    static std::vector<Fuel> readCSV(const std::string& filename);
};
