#pragma once
#include "Config.h"
#include <string>
#include <memory>
#include <curl/curl.h>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <map>
#include <chrono>
#include <vector>
#include <mutex>
#include <queue>
#include "RateLimiter.h"
#include "TradingRules.h"
#include "Logger.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

using json = nlohmann::json;

using websocket_client = websocketpp::client<websocketpp::config::asio_tls_client>;

struct OrderBookEntry {
    long id;
    std::string side;
    double price;
    double size;
};

struct OrderInfo {
    std::string side;
    int amount;
    double price;
    std::string symbol;
    std::string ordType;
    std::string status;
    std::chrono::system_clock::time_point last_updated;
};

class TechnicalIndicators {
public:
    static double calculateATR(const std::vector<double>& high, const std::vector<double>& low, const std::vector<double>& close, int period){
        if (high.size() != low.size() || high.size() != close.size() || high.size() < period + 1) {
        return 0.0;  // Invalid input
        }

        std::vector<double> tr(high.size());
        tr[0] = high[0] - low[0];  // First TR is simply the first day's range

        for (size_t i = 1; i < high.size(); ++i) {
            double hl = high[i] - low[i];
            double hc = std::abs(high[i] - close[i-1]);
            double lc = std::abs(low[i] - close[i-1]);
            tr[i] = std::max({hl, hc, lc});
        }

    // Calculate initial ATR
        double atr = std::accumulate(tr.begin(), tr.begin() + period, 0.0) / period;

    // Calculate subsequent ATRs
        for (size_t i = period; i < tr.size(); ++i) {
            atr = (atr * (period - 1) + tr[i]) / period;
        }

        return atr;

    }

    static double calculateRSI(const std::vector<double>& prices, int period = 14) {
        if (prices.size() < period + 1) {
            return 50.0; // Not enough data, return neutral RSI
        }

        double avg_gain = 0.0, avg_loss = 0.0;
        for (int i = 1; i <= period; ++i) {
            double change = prices[i] - prices[i-1];
            if (change > 0) {
                avg_gain += change;
            } else {
                avg_loss -= change;
            }
        }
        avg_gain /= period;
        avg_loss /= period;

        double rs = avg_gain / avg_loss;
        return 100.0 - (100.0 / (1.0 + rs));
    }
};

class BitMEXHFTBot {

public:
    BitMEXHFTBot();
    ~BitMEXHFTBot();
    void trade(const py::dict& market_data);
    void on_trade(const py::dict& trade);
    void on_order(const py::dict& order);
    void on_book(const py::dict& book);
    std::vector<py::dict> get_desired_orders();

private:
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl;
    websocket_client ws_client;
    websocket_client::connection_ptr ws_connection;
    std::unique_ptr<RateLimiter> rate_limiter;
    std::queue<std::chrono::steady_clock::time_point> request_queue;
    std::unique_ptr<ConfigManager> config_manager;

    TechnicalIndicators indicators;

    double initial_balance;
    double current_balance;
    double trade_amount;
    double savings;

    std::string api_base_url;
    std::string ws_base_url;
    std::string api_secret;
    std::string api_key;

    std::unordered_map<std::string, OrderInfo> open_orders;
    double traded_value;
    int quotes_submitted;

    std::mutex order_book_mutex;
    std::map<double, OrderBookEntry> order_book_bids;
    std::map<double, OrderBookEntry> order_book_asks;
    void connect_websocket();
    void on_message(websocketpp::connection_hdl, websocket_client::message_ptr msg);
    void subscribe_to_topics();
    void handle_instrument_update(const json& data);
    void handle_orderbook_update(const json& data);
    double get_market_price(const std::string& symbol = "XBTUSD");
    void place_order_with_retry(const std::string& side, int amount, double price, const std::string& symbol, const std::string& orderType);
    std::string analyze_order_book();
    bool check_stop_loss(double entry_price, double current_price, const std::string& side);
    bool check_take_profit(double entry_price, double current_price, const std::string& side);
    void set_dead_mans_switch(int timeout = 60000);
    
    // Market making methods
    void place_market_making_orders();
    void adjust_orders_based_on_imbalance();

    // Helper methods
    static size_t curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* s);
    std::string http_request(const std::string& url, const std::string& method, const std::string& data = "");

    // Order tracking method
    void track_order(const std::string& orderID);

    // Newly added functions
    void update_trading_strategy(double mid_price, double best_bid, double best_ask);
    void update_order_book(const std::vector<OrderBookEntry>& bids, const std::vector<OrderBookEntry>& asks);
    json get_position();
    json get_positions();

    // Added function declarations
    void amend_order(const std::string& orderID, int new_amount, double new_price);
    void cancel_order(const std::string& orderID);
    void manage_orders();
    void cancel_all_orders(const std::string& side);

    void start_heartbeat();

    // New function declarations
    bool is_overbought();
    bool is_oversold();
    void reducePosition(double currentQty, double markPrice, const std::string& direction);
    std::vector<double> getHistoricalPrices(int period);
    void closePosition(double currentQty, double markPrice, const std::string& direction);
    void partialClosePosition(double currentQty, double markPrice, const std::string& direction);
    void adjustOrders(double lastPrice, double markPrice);
    bool is_bullish_trend(double lastPrice, double markPrice, double fundingRate);
    bool is_bearish_trend(double lastPrice, double markPrice, double fundingRate);
    double calculate_position_risk(const json& position, double current_price);
    double calculatePositionSize(double lastPrice);
    void openNewPosition(const std::string& direction, double size);
    double get_account_balance();

    // Newly added function declarations
    bool check_rate_limit();
    bool place_orders();
    double get_current_price();
    void manage_positions(double current_price);

    // Added function declarations as per instructions
    void manage_single_position(const json& position, double current_price);
    void close_position(const std::string& symbol, int current_qty, const std::string& side, const std::string& reason);
    void partial_close_position(const std::string& symbol, int current_qty, const std::string& side, const std::string& reason);
    bool check_dynamic_take_profit(double entry_price, double current_price, const std::string& side, double unrealized_pnl, double position_risk);
    bool check_trailing_stop(const json& position, double current_price);
    void adjust_position_size(const std::string& symbol, int current_qty, const std::string& side, double position_risk);

    // Newly added function declarations as per instructions
    double calculate_unrealized_pnl(const json& position, double current_price);
    std::string get_sector(const std::string& symbol);
    void reduce_overall_risk(const json& positions, double current_price, double total_risk, double max_portfolio_risk);
    void reduce_overall_exposure(const json& positions, double current_price, double total_exposure, double max_exposure);
    void reduce_sector_exposure(const json& positions, double current_price, const std::string& sector, double exposure, double max_sector_exposure);
    double calculate_portfolio_beta(const json& positions);
    void adjust_portfolio_beta(const json& positions, double current_price, double current_beta, double target_beta);
    bool check_dynamic_stop_loss(double entry_price, double current_price, const std::string& side, double unrealized_pnl, double position_risk);
    void subscribe_to_order_book();

    // Additional variables needed for get_desired_orders
    double last_sma20;
    int order_id_counter;
    bool should_open_position(double current_price);

};
