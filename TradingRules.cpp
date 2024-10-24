#include "TradingRules.h"
#include <algorithm>
#include <cmath>

namespace TradingRules {

bool checkFatFingerProtection(const std::string& side, double price, double bestBid, double bestAsk, double markPrice) {
    if (side == "Buy") {
        double maxAllowedPrice = std::max(bestAsk, markPrice) * (1 + FAT_FINGER_PROTECTION_PERCENTAGE);
        return price <= maxAllowedPrice;
    } else if (side == "Sell") {
        double minAllowedPrice = std::min(bestBid, markPrice) * (1 - FAT_FINGER_PROTECTION_PERCENTAGE);
        return price >= minAllowedPrice;
    }
    return false;
}

bool checkOrderLimits(const std::string& symbol, int currentOpenOrders, int currentStopOrders) {
    return (currentOpenOrders < MAX_OPEN_ORDERS_PER_SYMBOL) && (currentStopOrders < MAX_STOP_ORDERS_PER_SYMBOL);
}

bool checkQVRThreshold(double quoteValue, double tradedValue, int quotesSubmitted) {
    if (quotesSubmitted <= FREE_QUOTES_PER_HOUR) {
        return true;
    }
    double qvr = (quotesSubmitted - FREE_QUOTES_PER_HOUR) / (tradedValue + 1e-9); // Add small value to avoid division by zero
    return qvr <= QVR_THRESHOLD;
}

}