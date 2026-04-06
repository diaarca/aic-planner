#pragma once

#include "csv_object.hpp"
#include <map>
#include <string>

struct Region : public CSVObject
{
    double base_power = 0;
    double storage = 0;

    static Region readCSV(const std::string& filename);

    // CSVObject implementation
    std::string get_title() const override { return "Region"; }
    std::vector<std::string> get_headers() const override;
    std::vector<std::string> get_values() const override;
    void load(const std::map<std::string, std::string>& row_data) override;
    std::string get_key() const override { return "region"; }
};
