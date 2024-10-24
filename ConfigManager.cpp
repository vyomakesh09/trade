#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <algorithm>

ConfigManager::ConfigManager(const std::string& filename) {
    load_config(filename);
}

void ConfigManager::load_config(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open config file " << filename << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '=')) {
            std::string value;
            if (std::getline(is_line, value)) {
                config_map[key] = value;
            }
        }
    }
}

double ConfigManager::get_double(const std::string& key, double default_value) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        try {
            return std::stod(it->second);
        } catch (const std::exception& e) {
            std::cerr << "Error: Unable to convert " << key << " to double. Using default value." << std::endl;
        }
    }
    return default_value;
}

int ConfigManager::get_int(const std::string& key, int default_value) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception& e) {
            std::cerr << "Error: Unable to convert " << key << " to int. Using default value." << std::endl;
        }
    }
    return default_value;
}

std::string ConfigManager::get_string(const std::string& key, const std::string& default_value) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        return it->second;
    }
    return default_value;
}

bool ConfigManager::get_bool(const std::string& key, bool default_value) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        if (value == "true" || value == "1") {
            return true;
        } else if (value == "false" || value == "0") {
            return false;
        }
    }
    return default_value;
}