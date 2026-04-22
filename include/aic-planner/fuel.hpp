#pragma once

#include <iostream>
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

    friend std::ostream& operator<<(std::ostream& os, const Fuel& f);
    static void print_table(const std::vector<Fuel>& fuels);
};
