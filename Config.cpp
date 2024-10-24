#include "Config.h"

namespace Config {
    const std::vector<std::string> instruments = {"XBTUSD", "ETHUSD"};
    const std::vector<std::string> additional_topics = {"trade", "liquidation"};

    const double ORDER_SPREAD = 0.001; // 0.1% spread
    const double RISK_PER_TRADE = 0.01; // 1% risk per trade

    const double MAX_FUNDING_RATE = 0.01; // 1% maximum funding rate
    const double MAX_PRICE_DEVIATION = 0.05; // 5% maximum price deviation
    const int ORDER_SIZE = 100; // Default order size

    const double MAX_PORTFOLIO_RISK = 0.05; // 5% maximum portfolio risk
    const double MAX_PORTFOLIO_EXPOSURE = 0.5; // 50% maximum portfolio exposure
    const double MAX_SECTOR_EXPOSURE = 0.2; // 20% maximum sector exposure
    const double TARGET_PORTFOLIO_BETA = 1.0; // Target portfolio beta
    const double BETA_TOLERANCE = 0.1; // Beta tolerance

    const double BUY_IMBALANCE_THRESHOLD = 0.6; // 60% buy imbalance threshold
    const double SELL_IMBALANCE_THRESHOLD = 0.4; // 40% sell imbalance threshold

    const double INITIAL_BALANCE = 1000.0; // Initial account balance
    const double TRADE_PERCENTAGE = 0.02; // 2% of balance per trade
    const double VOLUME_IMBALANCE_THRESHOLD = 0.7; // 70% volume imbalance threshold
    const double STOP_LOSS_PERCENTAGE = 0.02; // 2% stop loss
    const double TAKE_PROFIT_PERCENTAGE = 0.03; // 3% take profit
    const double PRICE_OFFSET = 0.001; // 0.1% price offset for limit orders
    const int TRADE_INTERVAL = 60; // 60 seconds between trades
    const int ERROR_WAIT_TIME = 5; // 5 seconds wait time after error
    const double MAX_LEVERAGE = 10.0; // Maximum leverage
    const double MAX_PNL_PERCENTAGE = 0.05; // 5% maximum PnL percentage
    const double TARGET_LEVERAGE = 5.0; // Target leverage
    const int DEAD_MANS_SWITCH_TIMEOUT = 3600; // 1 hour dead man's switch timeout
    const int API_RETRY_DELAY = 1000; // 1 second delay between API retries
    const std::string API_KEY = "your_api_key_here";
    const std::string API_SECRET = "your_api_secret_here";
    const bool USE_TESTNET = true; // Use testnet by default
}
