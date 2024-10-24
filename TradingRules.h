#pragma once
#include <string>

namespace TradingRules {
    constexpr double FAT_FINGER_PROTECTION_PERCENTAGE = 0.05;
    constexpr int MAX_OPEN_ORDERS_PER_SYMBOL = 200;
    constexpr int MAX_STOP_ORDERS_PER_SYMBOL = 10;
    constexpr double QVR_THRESHOLD = 10000.0; // 10,000 quotes per XBT traded per hour
    constexpr int FREE_QUOTES_PER_HOUR = 3600;

    bool checkFatFingerProtection(const std::string& side, double price, double bestBid, double bestAsk, double markPrice);
    bool checkOrderLimits(const std::string& symbol, int currentOpenOrders, int currentStopOrders);
    bool checkQVRThreshold(double quoteValue, double tradedValue, int quotesSubmitted);
}