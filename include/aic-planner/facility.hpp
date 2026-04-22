#pragma once

#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Facility
{
    std::string name;
    double power;

    static std::map<std::string, double> readCSV(const std::string& filename);

    friend std::ostream& operator<<(std::ostream& os, const Facility& f);
    static void print_table(const std::map<std::string, double>& facilities);
};
