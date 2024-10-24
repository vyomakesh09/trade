#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "BitMEXHFTBot.h"
#include "RateLimiter.h"
#include "Logger.h"
#include "Config.h"
#include "ConfigManager.h"

namespace py = pybind11;

PYBIND11_MODULE(bitmex_hft_bot, m) {
    py::class_<BitMEXHFTBot>(m, "BitMEXHFTBot")
        .def(py::init<>())
        .def("trade", &BitMEXHFTBot::trade)
        .def("get_desired_orders", &BitMEXHFTBot::get_desired_orders)
        .def("on_trade", &BitMEXHFTBot::on_trade)
        .def("on_order", &BitMEXHFTBot::on_order)
        .def("on_book", &BitMEXHFTBot::on_book);

    py::class_<RateLimiter>(m, "RateLimiter")
        .def(py::init<>())
        .def("add_request", &RateLimiter::add_request)
        .def("get_reset_interval", &RateLimiter::get_reset_interval);

    py::enum_<LogLevel>(m, "LogLevel")
        .value("DEBUG", LogLevel::DEBUG)
        .value("INFO", LogLevel::INFO)
        .value("WARNING", LogLevel::WARNING)
        .value("ERROR", LogLevel::ERROR);

    m.def("log", &Logger::log, "Log a message with specified log level");

    py::class_<bitmex::Config>(m, "Config")
        .def_static("get_initial_balance", &bitmex::Config::get_initial_balance)
        .def_static("get_trade_percentage", &bitmex::Config::get_trade_percentage)
        .def_static("get_use_testnet", &bitmex::Config::get_use_testnet)
        .def_static("get_stop_loss_percentage", &bitmex::Config::get_stop_loss_percentage)
        .def_static("get_take_profit_percentage", &bitmex::Config::get_take_profit_percentage)
        .def_static("get_dead_mans_switch_timeout", &bitmex::Config::get_dead_mans_switch_timeout)
        .def_static("get_error_wait_time", &bitmex::Config::get_error_wait_time)
        .def_static("get_api_retry_delay", &bitmex::Config::get_api_retry_delay)
        .def_static("get_risk_per_trade", &bitmex::Config::get_risk_per_trade);

    py::class_<ConfigManager>(m, "ConfigManager")
        .def(py::init<const std::string&>())
        .def("get_double", &ConfigManager::get_double)
        .def("get_int", &ConfigManager::get_int)
        .def("get_string", &ConfigManager::get_string)
        .def("get_bool", &ConfigManager::get_bool);
}
