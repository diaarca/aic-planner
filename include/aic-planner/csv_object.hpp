#pragma once

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

class CSVObject
{
  public:
    virtual ~CSVObject() = default;

    // Returns the title of the table for this object type
    virtual std::string get_title() const = 0;

    // Returns the headers for display
    virtual std::vector<std::string> get_headers() const = 0;

    // Returns the values for display
    virtual std::vector<std::string> get_values() const = 0;

    // Load data from a map of header names to values
    virtual void load(const std::map<std::string, std::string>& row_data) = 0;

    // Return a unique key for this object (e.g., its name)
    virtual std::string get_key() const = 0;

    // Modular operator<< to print a single instance
    friend std::ostream& operator<<(std::ostream& os, const CSVObject& obj)
    {
        auto headers = obj.get_headers();
        auto values = obj.get_values();
        for (size_t i = 0; i < headers.size(); ++i)
        {
            os << std::left << std::setw(15)
               << (headers[i].length() > 15 ? headers[i].substr(0, 15)
                                            : headers[i])
               << ": "
               << (values[i].length() > 15 ? values[i].substr(0, 15)
                                           : values[i])
               << (i == headers.size() - 1 ? "" : " | ");
        }
        return os;
    }

    // Generic function to display a table of any objects inheriting from
    // CSVObject
    template <typename T>
    static void print_table(const std::vector<T>& objects,
                            std::ostream& os = std::cout)
    {
        if (objects.empty())
            return;

        const CSVObject& first = static_cast<const CSVObject&>(objects[0]);
        os << "\n--- " << first.get_title() << " ---\n";
        auto headers = first.get_headers();

        // Print header row
        for (const auto& h : headers)
        {
            os << std::left << std::setw(15)
               << (h.length() > 15 ? h.substr(0, 15) : h) << " | ";
        }
        os << "\n" << std::string(headers.size() * 18, '-') << "\n";

        // Print each object as a row
        for (const auto& obj : objects)
        {
            const CSVObject& base_obj = static_cast<const CSVObject&>(obj);
            auto values = base_obj.get_values();
            for (const auto& v : values)
            {
                os << std::left << std::setw(15)
                   << (v.length() > 15 ? v.substr(0, 15) : v) << " | ";
            }
            os << "\n";
        }
    }

    // Generic method to read a CSV and return a map of objects
    template <typename T>
    static std::map<std::string, T>
    read_csv_map(const std::string& filename,
                 const std::vector<std::string>& expected_headers)
    {
        auto data = read_file(filename);
        if (data.empty())
            return {};

        const auto& file_headers = data[0];

        // Strict header check as requested
        if (file_headers.size() != expected_headers.size())
        {
            throw std::runtime_error(
                "Header count mismatch in " + filename + ". Expected " +
                std::to_string(expected_headers.size()) + ", found " +
                std::to_string(file_headers.size()));
        }

        std::map<std::string, int> header_to_idx;
        for (int i = 0; i < (int)file_headers.size(); ++i)
        {
            header_to_idx[file_headers[i]] = i;
        }

        for (const auto& expected : expected_headers)
        {
            if (header_to_idx.find(expected) == header_to_idx.end())
            {
                throw std::runtime_error("Missing required header '" +
                                         expected + "' in " + filename);
            }
        }

        std::map<std::string, T> results;
        for (size_t i = 1; i < data.size(); ++i)
        {
            const auto& row = data[i];
            if (row.size() != file_headers.size())
                continue;

            std::map<std::string, std::string> row_map;
            for (size_t j = 0; j < file_headers.size(); ++j)
            {
                row_map[file_headers[j]] = row[j];
            }

            T obj;
            obj.load(row_map);
            results[obj.get_key()] = obj;
        }
        return results;
    }

    // Generic method to read a CSV and return a vector of objects
    template <typename T>
    static std::vector<T>
    read_csv_vector(const std::string& filename,
                    const std::vector<std::string>& expected_headers)
    {
        auto data = read_file(filename);
        if (data.empty())
            return {};

        const auto& file_headers = data[0];

        if (file_headers.size() != expected_headers.size())
        {
            throw std::runtime_error("Header count mismatch in " + filename);
        }

        std::map<std::string, int> header_to_idx;
        for (int i = 0; i < (int)file_headers.size(); ++i)
        {
            header_to_idx[file_headers[i]] = i;
        }

        for (const auto& expected : expected_headers)
        {
            if (header_to_idx.find(expected) == header_to_idx.end())
            {
                throw std::runtime_error("Missing required header '" +
                                         expected + "' in " + filename);
            }
        }

        std::vector<T> results;
        for (size_t i = 1; i < data.size(); ++i)
        {
            const auto& row = data[i];
            if (row.size() != file_headers.size())
                continue;

            std::map<std::string, std::string> row_map;
            for (size_t j = 0; j < file_headers.size(); ++j)
            {
                row_map[file_headers[j]] = row[j];
            }

            T obj;
            obj.load(row_map);
            results.push_back(obj);
        }
        return results;
    }

    // Helper to get headers directly from a file
    static std::vector<std::string>
    get_file_headers(const std::string& filename)
    {
        std::ifstream file(filename);
        std::string line;
        if (std::getline(file, line))
        {
            return parse_line(line);
        }
        return {};
    }

    // Static helper to read lines from a CSV file
    static std::vector<std::vector<std::string>>
    read_file(const std::string& filename)
    {
        std::vector<std::vector<std::string>> rows;
        std::ifstream file(filename);
        if (!file.is_open())
        {
            return rows;
        }
        std::string line;
        while (std::getline(file, line))
        {
            rows.push_back(parse_line(line));
        }
        return rows;
    }

  protected:
    static std::vector<std::string> parse_line(const std::string& line)
    {
        std::vector<std::string> result;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            const std::string whitespace = " \t\n\r";
            size_t first = item.find_first_not_of(whitespace);
            if (std::string::npos == first)
            {
                result.push_back("");
                continue;
            }
            size_t last = item.find_last_not_of(whitespace);
            result.push_back(item.substr(first, (last - first + 1)));
        }
        return result;
    }
};
