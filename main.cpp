#include "BitMEXHFTBot.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

void load_env_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open config file: " + filename);
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '=')) {
            std::string value;
            if (std::getline(is_line, value)) {
                setenv(key.c_str(), value.c_str(), 1);
                std::cout << "Loaded environment variable: " << key << "=" << value << std::endl;
            }
        }
    }
}

int main() {
    try {
        std::filesystem::path config_path = std::filesystem::current_path().parent_path() / "config.env";
        std::cout << "Attempting to open config file: " << config_path << std::endl;
        
        if (!std::filesystem::exists(config_path)) {
            std::cerr << "Config file not found: " << config_path << std::endl;
            return 1;
        }
        
        load_env_file(config_path.string());
        BitMEXHFTBot bot;
        bot.trade(py::dict());
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}