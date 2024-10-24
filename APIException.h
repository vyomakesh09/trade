#pragma once

#include <stdexcept>
#include <string>

class APIException : public std::runtime_error {
public:
    explicit APIException(const std::string& message) : std::runtime_error(message) {}
};