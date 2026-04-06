#pragma once

#include "csv_object.hpp"
#include <map>
#include <string>
#include <vector>

struct Facility : public CSVObject
{
    std::string name;
    double power;

    static std::map<std::string, double> readCSV(const std::string& filename);

    // CSVObject implementation
    std::string get_title() const override { return "Facilities"; }
    std::vector<std::string> get_headers() const override;
    std::vector<std::string> get_values() const override;
    void load(const std::map<std::string, std::string>& row_data) override;
    std::string get_key() const override { return name; }
};
