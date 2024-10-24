#include "Logger.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

std::ofstream Logger::logFile;

void Logger::init(const std::string& filename) {
    logFile.open(filename, std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    } else {
        std::cout << "Log file opened successfully: " << filename << std::endl;
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::string logMessage = getTimestamp() + " [" + levelToString(level) + "] " + message + "\n";
    
    if (logFile.is_open()) {
        logFile << logMessage;
        logFile.flush();
    }
    
    // Also print to console for immediate feedback
    std::cout << logMessage << std::flush;
}

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}