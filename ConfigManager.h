#pragma once

#include <string>
#include <unordered_map>

class ConfigManager {
public:
    ConfigManager(const std::string& config_file);

    double get_double(const std::string& key, double default_value) const;
    int get_int(const std::string& key, int default_value) const;
    std::string get_string(const std::string& key, const std::string& default_value) const;
    bool get_bool(const std::string& key, bool default_value) const;

private:
    std::unordered_map<std::string, std::string> config_map;
    void load_config(const std::string& filename);
};
