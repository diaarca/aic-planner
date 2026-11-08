#pragma once

#include "mineral.hpp"
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace aic
{

struct Region
{
    std::vector<Mineral> resources;
    std::string catalog;
    std::string market_region;
    std::string currency;
    bool automatic_recipes = false;
    std::map<std::string, int> settlement_levels;
    std::vector<std::string> active_activities;
    std::vector<std::string> active_time_ids;
    std::vector<std::string> enabled_facilities;
    std::vector<std::string> enabled_recipes;
    double base_power = 0;
    double storage = 0;

    static Region read_json(const std::string &filename);

    friend std::ostream &operator<<(std::ostream &os, const Region &r);
    static void print_table(const Region &r);
};

} // namespace aic
