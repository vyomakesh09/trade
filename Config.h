#pragma once
#include <vector>
#include <string>

namespace Config {
    const std::vector<std::string> instruments = {"XBTUSD", "ETHUSD"};
    const std::vector<std::string> additional_topics = {"trade", "liquidation"};

    const double ORDER_SPREAD = 0.01;
    const double RISK_PER_TRADE = 0.01;

    const double MAX_FUNDING_RATE = 0.001;
    const double MAX_PRICE_DEVIATION = 0.05;
    const int ORDER_SIZE = 100;

    const double MAX_PORTFOLIO_RISK = 0.1;
    const double MAX_PORTFOLIO_EXPOSURE = 0.5;
    const double MAX_SECTOR_EXPOSURE = 0.3;
    const double TARGET_PORTFOLIO_BETA = 1.0;
    const double BETA_TOLERANCE = 0.1;

    const double BUY_IMBALANCE_THRESHOLD = 0.6;
    const double SELL_IMBALANCE_THRESHOLD = 0.4;

    const double INITIAL_BALANCE = 10000.0;
    const double TRADE_PERCENTAGE = 0.1;
    const double VOLUME_IMBALANCE_THRESHOLD = 0.7;
    const double STOP_LOSS_PERCENTAGE = 0.02;
    const double TAKE_PROFIT_PERCENTAGE = 0.03;
    const double PRICE_OFFSET = 0.005;
    const int TRADE_INTERVAL = 5;
    const int ERROR_WAIT_TIME = 5;
    const double MAX_LEVERAGE = 10.0;
    const double MAX_PNL_PERCENTAGE = 0.2;
    const double TARGET_LEVERAGE = 5.0;
    const int DEAD_MANS_SWITCH_TIMEOUT = 3600;
    const int API_RETRY_DELAY = 1000;
    const std::string API_KEY = "";
    const std::string API_SECRET = "";
    const bool USE_TESTNET = true;
}