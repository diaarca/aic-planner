#pragma once

#include "csv_object.hpp"
#include <string>
#include <vector>

struct Fuel : public CSVObject
{
    std::string name;
    double power;
    double duration;

    Fuel() = default;
    Fuel(std::string n, double p, double d) : name(n), power(p), duration(d) {}

    static std::vector<Fuel> readCSV(const std::string& filename);

    // CSVObject implementation
    std::string get_title() const override { return "Fuels"; }
    std::vector<std::string> get_headers() const override;
    std::vector<std::string> get_values() const override;
    void load(const std::map<std::string, std::string>& row_data) override;
    std::string get_key() const override { return name; }
};
