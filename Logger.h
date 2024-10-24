#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static void init(const std::string& filename);
    static void log(LogLevel level, const std::string& message);

private:
    static std::ofstream logFile;
    static std::string getTimestamp();
    static std::string levelToString(LogLevel level);
};

#define LOG_DEBUG(message) Logger::log(LogLevel::DEBUG, message)
#define LOG_INFO(message) Logger::log(LogLevel::INFO, message)
#define LOG_WARNING(message) Logger::log(LogLevel::WARNING, message)
#define LOG_ERROR(message) Logger::log(LogLevel::ERROR, message)