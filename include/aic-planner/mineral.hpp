#pragma once

#include <iostream>
#include <string>
#include <vector>

struct Mineral
{
    std::string name;
    double limit;

    Mineral() = default;
    Mineral(std::string n, double l) : name(n), limit(l) {}

    static std::vector<Mineral> readCSV(const std::string& filename);

    friend std::ostream& operator<<(std::ostream& os, const Mineral& m);
    static void print_table(const std::vector<Mineral>& minerals);
};
