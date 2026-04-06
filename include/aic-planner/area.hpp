#pragma once

#include "csv_object.hpp"
#include <map>
#include <string>
#include <vector>

struct Area : public CSVObject
{
    std::string name;
    double pac_width;
    double pac_height;
    double pac_depot_width;
    double pac_depot_height;
    std::map<std::string, double> area_facilities;

    static std::vector<Area> readCSV(const std::string& filename);

    // CSVObject implementation
    std::string get_title() const override { return "Areas"; }
    std::vector<std::string> get_headers() const override;
    std::vector<std::string> get_values() const override;
    void load(const std::map<std::string, std::string>& row_data) override;
    std::string get_key() const override { return name; }
};
