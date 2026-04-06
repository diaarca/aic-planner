#pragma once

#include "csv_object.hpp"
#include "mineral.hpp"
#include <map>
#include <string>
#include <vector>

// Data structure for a product
struct Product : public CSVObject
{
    std::string name;
    double value;
    std::map<std::string, double> mineral_consumption;
    double production_time; // Time in seconds to produce one unit

    // Factory related information
    double factory_width;
    double factory_height;
    double factory_depot;
    std::map<std::string, double> factory_facilities;

    static std::vector<Product>
    readCSV(const std::string& filename,
            const std::vector<Mineral>& mineral_limits);

    // CSVObject implementation
    std::string get_title() const override { return "Products"; }
    std::vector<std::string> get_headers() const override;
    std::vector<std::string> get_values() const override;
    void load(const std::map<std::string, std::string>& row_data) override;
    std::string get_key() const override { return name; }

    // Categorize consumption fields into minerals and facilities
    void categorize_consumption(const std::vector<Mineral>& mineral_limits);

  private:
    std::map<std::string, double> _all_dynamic;
};
