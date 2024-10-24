#include "BitMEXHFTBot.h"
#include "Config.h"
#include "APIException.h"
#include "ConfigManager.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <map>
#include <mutex>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace pybind11::literals;

std::string hex_encode(unsigned char* data, size_t len) {
    std::string result;
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        result.push_back(hex_chars[(data[i] & 0xF0) >> 4]);
        result.push_back(hex_chars[data[i] & 0x0F]);
    }
    return result;
}

BitMEXHFTBot::BitMEXHFTBot()
    : config_manager(std::make_unique<ConfigManager>("config.ini")),
      initial_balance(config_manager->get_double("initial_balance", 10000.0)),
      current_balance(initial_balance),
      trade_amount(current_balance * config_manager->get_double("trade_percentage", 0.1)),
      rate_limiter(std::make_unique<RateLimiter>()) {
    
    Config::initialize();
    
    if (config_manager->get_bool("use_testnet", true)) {
        api_base_url = "https://testnet.bitmex.com/api/v1";
        ws_base_url = "wss://testnet.bitmex.com/realtime";
    } else {
        api_base_url = "https://www.bitmex.com/api/v1";
        ws_base_url = "wss://www.bitmex.com/realtime";
    }
    
    Logger::init("bitmex_hft_bot.log");
    
    LOG_INFO("API Key: " + std::string(std::getenv("API_KEY")));
    LOG_INFO("API Secret: " + std::string(std::getenv("API_SECRET")));

    LOG_INFO("BitMEX HFT Bot initialized with initial balance: " + std::to_string(initial_balance));

    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    ws_client.init_asio();
    ws_client.set_tls_init_handler([](websocketpp::connection_hdl) {
        return websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
    });

    connect_websocket();
}

BitMEXHFTBot::~BitMEXHFTBot() {
    if (ws_connection && ws_connection->get_state() == websocketpp::session::state::open) {
        ws_connection->close(websocketpp::close::status::normal, "Closing connection");
    }
}
void BitMEXHFTBot::connect_websocket() {
    const int MAX_RECONNECT_ATTEMPTS = 10;
    int reconnect_attempts = 0;
    std::chrono::milliseconds initial_backoff(100);
    std::chrono::seconds max_backoff(60);

    auto calculate_backoff = [&](int attempt) -> std::chrono::milliseconds {
        auto backoff = initial_backoff * static_cast<int>(std::pow(2, attempt));
        return std::min(backoff, std::chrono::duration_cast<std::chrono::milliseconds>(max_backoff));
    };

    while (reconnect_attempts < MAX_RECONNECT_ATTEMPTS) {
        websocketpp::lib::error_code ec;
        ws_connection = ws_client.get_connection(ws_base_url, ec);
        if (ec) {
            LOG_ERROR("Could not create connection: " + ec.message());
            auto backoff = calculate_backoff(reconnect_attempts);
            LOG_INFO("Backing off for " + std::to_string(backoff.count()) + " ms before reconnection attempt " + std::to_string(reconnect_attempts + 1));
            std::this_thread::sleep_for(backoff);
            ++reconnect_attempts;
            continue;
        }

        ws_connection->set_open_handler([this, &reconnect_attempts](websocketpp::connection_hdl) {
            LOG_INFO("WebSocket connection opened successfully");
            reconnect_attempts = 0; // Reset attempts on successful connection
            LOG_INFO("Reconnection attempts reset to 0");
            try {
                LOG_INFO("Subscribing to topics...");
                subscribe_to_topics();
                LOG_INFO("Subscribing to order book...");
                subscribe_to_order_book();
                LOG_INFO("Subscriptions completed successfully");
                
                // Start heartbeat
                start_heartbeat();
            } catch (const std::exception& e) {
                LOG_ERROR("Error in open handler: " + std::string(e.what()));
                ws_connection->close(websocketpp::close::status::internal_endpoint_error, 
                                     "Error in open handler");
            }
        });

        ws_connection->set_message_handler([this](websocketpp::connection_hdl, websocket_client::message_ptr msg) {
            try {
                LOG_DEBUG("Received message: " + msg->get_payload());
                this->on_message(websocketpp::connection_hdl(), msg);
            } catch (const std::exception& e) {
                LOG_ERROR("Error in message handler: " + std::string(e.what()));
            }
        });

        ws_connection->set_close_handler([this, &reconnect_attempts, &calculate_backoff](websocketpp::connection_hdl) {
            LOG_WARNING("WebSocket connection closed. Attempting to reconnect...");
            auto backoff = calculate_backoff(reconnect_attempts);
            LOG_INFO("Backing off for " + std::to_string(backoff.count()) + " ms before reconnection attempt " + std::to_string(reconnect_attempts + 1));
            std::this_thread::sleep_for(backoff);
            ++reconnect_attempts;
            LOG_INFO("Initiating reconnection attempt " + std::to_string(reconnect_attempts));
            this->connect_websocket();
        });

        ws_connection->set_fail_handler([this, &reconnect_attempts, &calculate_backoff](websocketpp::connection_hdl) {
            LOG_ERROR("WebSocket connection failed. Attempting to reconnect...");
            auto backoff = calculate_backoff(reconnect_attempts);
            LOG_INFO("Backing off for " + std::to_string(backoff.count()) + " ms before reconnection attempt " + std::to_string(reconnect_attempts + 1));
            std::this_thread::sleep_for(backoff);
            ++reconnect_attempts;
            LOG_INFO("Initiating reconnection attempt " + std::to_string(reconnect_attempts));
            this->connect_websocket();
        });

        try {
            LOG_INFO("Attempting to connect to WebSocket...");
            ws_client.connect(ws_connection);
            LOG_INFO("WebSocket connection established. Starting client run loop...");
            ws_client.run();
            LOG_INFO("WebSocket client run loop completed successfully");
            break; // If we reach here, connection was successful
        } catch (const websocketpp::exception& e) {
            LOG_ERROR("WebSocket exception: " + std::string(e.what()));
            auto backoff = calculate_backoff(reconnect_attempts);
            LOG_INFO("Backing off for " + std::to_string(backoff.count()) + " ms before reconnection attempt " + std::to_string(reconnect_attempts + 1));
            std::this_thread::sleep_for(backoff);
            ++reconnect_attempts;
        } catch (const std::exception& e) {
            LOG_ERROR("Standard exception: " + std::string(e.what()));
            auto backoff = calculate_backoff(reconnect_attempts);
            LOG_INFO("Backing off for " + std::to_string(backoff.count()) + " ms before reconnection attempt " + std::to_string(reconnect_attempts + 1));
            std::this_thread::sleep_for(backoff);
            ++reconnect_attempts;
        }
    }

    if (reconnect_attempts == MAX_RECONNECT_ATTEMPTS) {
        LOG_ERROR("Failed to connect to WebSocket after " + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts");
        throw std::runtime_error("Failed to connect to WebSocket after maximum attempts");
    }
}

void BitMEXHFTBot::on_message(websocketpp::connection_hdl, websocket_client::message_ptr msg) {
    json data = json::parse(msg->get_payload());
    if (data.contains("table")) {
        if (data["table"] == "instrument") {
            handle_instrument_update(data);
        } else if (data["table"] == "orderBookL2_25" || data["table"] == "orderBookL2") {
            std::vector<OrderBookEntry> bids, asks;
            update_order_book(bids, asks);
        }
    }
}

void BitMEXHFTBot::subscribe_to_topics() {
    json subscription = {
        {"op", "subscribe"},
        {"args", {}}
    };

    for (const auto& instrument : Config::instruments) {
        subscription["args"].push_back("instrument:" + instrument);
        subscription["args"].push_back("orderBookL2_25:" + instrument);
    }

    for (const auto& topic :  Config::additional_topics) {
        subscription["args"].push_back(topic);
    }

    if (!subscription["args"].empty()) {
        ws_connection->send(subscription.dump());
        LOG_INFO("Subscribed to topics: " + subscription["args"].dump());
    } else {
        LOG_WARNING("No topics to subscribe to. Check configuration.");
    }
}

void BitMEXHFTBot::handle_instrument_update(const json& data) {
    double lastPrice = 0.0;
    double markPrice = 0.0;
    double fundingRate = 0.0;

    if (data.contains("data") && !data["data"].empty()) {
        const auto& instrument = data["data"][0];
        
        if (instrument.contains("lastPrice")) {
            lastPrice = instrument["lastPrice"].get<double>();
            LOG_INFO("Last price updated: " + std::to_string(lastPrice));
        }
        
        if (instrument.contains("markPrice")) {
            markPrice = instrument["markPrice"].get<double>();
            LOG_INFO("Mark price updated: " + std::to_string(markPrice));
        }
        
        if (instrument.contains("fundingRate")) {
            fundingRate = instrument["fundingRate"].get<double>();
            LOG_INFO("Funding rate updated: " + std::to_string(fundingRate));
        }
        
        // Update internal state or trigger trading logic based on new instrument data
        update_trading_strategy(lastPrice, markPrice, fundingRate);
    }
}

void BitMEXHFTBot::handle_orderbook_update(const json& data) {
    if (!data.contains("data") || data["data"].empty()) {
        return;
    }

    std::vector<OrderBookEntry> bids;
    std::vector<OrderBookEntry> asks;
    bids.reserve(data["data"].size());
    asks.reserve(data["data"].size());

    for (const auto& entry : data["data"]) {
        if (!entry.contains("id") || !entry.contains("side") || !entry.contains("size") || !entry.contains("price")) {
            continue;
        }

        OrderBookEntry obe{
            entry["id"].get<long>(),
            entry["side"].get<std::string>(),
            entry["size"].get<double>(),
            entry["price"].get<double>()
        };

        if (obe.side == "Buy") {
            bids.push_back(std::move(obe));
        } else if (obe.side == "Sell") {
            asks.push_back(std::move(obe));
        }
    }

    // Update internal order book state
    update_order_book(bids, asks);
}

json BitMEXHFTBot::get_position() {
    if (rate_limiter->add_request()) {
        std::string endpoint = "/position?filter={\"symbol\":\"XBTUSD\"}";
        std::string response = http_request(endpoint, "GET");
        json result = json::parse(response);
        
        if (result.is_array() && !result.empty()) {
            LOG_INFO("Retrieved position: " + result[0].dump());
            return result[0];
        } else if (result.is_array() && result.empty()) {
            LOG_INFO("No open position for XBTUSD");
            return json::object();
        } else if (result.contains("error")) {
            LOG_WARNING("Error retrieving position: " + result["error"]["message"].get<std::string>());
            return json::object();
        } else {
            LOG_WARNING("Unexpected response format when retrieving position");
            return json::object();
        }
    } else {
        LOG_ERROR("Rate limit exceeded while trying to get position");
        throw std::runtime_error("Rate limit exceeded while trying to get position");
    }
}
json BitMEXHFTBot::get_positions() {
    if (rate_limiter->add_request()) {
        std::string endpoint = "/position";
        std::string response = http_request(api_base_url + endpoint, "GET");
        json result = json::parse(response);
        
        LOG_INFO("Retrieved positions: " + result.dump());
        return result;
    } else {
        LOG_ERROR("Rate limit exceeded while trying to get positions");
        throw std::runtime_error("Rate limit exceeded while trying to get positions");
    }
}

void BitMEXHFTBot::update_trading_strategy(double lastPrice, double markPrice, double fundingRate) {
    LOG_INFO("Updating trading strategy with lastPrice: " + std::to_string(lastPrice) + 
             ", markPrice: " + std::to_string(markPrice) + 
             ", fundingRate: " + std::to_string(fundingRate));

    // Get current position
    json position = get_position();
    
    if (!position.empty()) {
        double currentQty = position["currentQty"].get<double>();
        double avgEntryPrice = position["avgEntryPrice"].get<double>();
        
        // Calculate unrealized PNL
        double unrealizedPnl = currentQty * (markPrice - avgEntryPrice);
        
        // Implement sophisticated C++ strategies
        if (std::abs(fundingRate) > Config::MAX_FUNDING_RATE) {
            if (fundingRate > 0 && currentQty > 0 && is_overbought()) {
                LOG_INFO("Market conditions suggest reducing long position.");
                reducePosition(currentQty, markPrice, "long");
            } else if (fundingRate < 0 && currentQty < 0 && is_oversold()) {
                LOG_INFO("Market conditions suggest reducing short position.");
                reducePosition(currentQty, markPrice, "short");
            }
        }
        
        // Implement dynamic stop loss and take profit based on volatility
        std::vector<double> historical_prices = getHistoricalPrices(14);
        std::vector<double> high_prices, low_prices, close_prices;
        for (const auto& price : historical_prices) {
            high_prices.push_back(price);
            low_prices.push_back(price);
            close_prices.push_back(price);
        }
        double atr = indicators.calculateATR(high_prices, low_prices, close_prices, 14);
        double dynamicStopLoss = Config::STOP_LOSS_PERCENTAGE * atr;
        double dynamicTakeProfit = Config::TAKE_PROFIT_PERCENTAGE * atr;

        if (currentQty > 0) {
            if (markPrice <= avgEntryPrice * (1 - dynamicStopLoss)) {
                LOG_INFO("Dynamic stop loss triggered for long position.");
                closePosition(currentQty, markPrice, "long");
            } else if (markPrice >= avgEntryPrice * (1 + dynamicTakeProfit)) {
                LOG_INFO("Dynamic take profit triggered for long position.");
                partialClosePosition(currentQty, markPrice, "long");
            }
        } else if (currentQty < 0) {
            if (markPrice >= avgEntryPrice * (1 + dynamicStopLoss)) {
                LOG_INFO("Dynamic stop loss triggered for short position.");
                closePosition(currentQty, markPrice, "short");
            } else if (markPrice <= avgEntryPrice * (1 - dynamicTakeProfit)) {
                LOG_INFO("Dynamic take profit triggered for short position.");
                partialClosePosition(currentQty, markPrice, "short");
            }
        }
        
        // Implement logic for adjusting existing positions based on market conditions
        double priceDeviation = std::abs(lastPrice - markPrice) / markPrice;
        if (priceDeviation > Config::MAX_PRICE_DEVIATION) {
            LOG_INFO("Significant price deviation detected. Adjusting orders.");
            adjustOrders(lastPrice, markPrice);
        }
    } else {
        // No current position, consider opening a new one based on market conditions
        if (is_bullish_trend(lastPrice, markPrice, fundingRate)) {
            LOG_INFO("Favorable conditions for opening a long position.");
            openNewPosition("long", calculatePositionSize(lastPrice));
        } else if (is_bearish_trend(lastPrice, markPrice, fundingRate)) {
            LOG_INFO("Favorable conditions for opening a short position.");
            openNewPosition("short", calculatePositionSize(lastPrice));
        }
    }
}

double BitMEXHFTBot::get_market_price(const std::string& symbol) {
    if (rate_limiter->add_request()) {
        std::string endpoint = "/instrument?symbol=" + symbol;
        std::string response = http_request(endpoint, "GET");
        json data = json::parse(response);
        return data[0]["lastPrice"].get<double>();
    } else {
        LOG_WARNING("Rate limit exceeded when getting market price. Retrying...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return get_market_price(symbol);  // Recursive call to retry
    }
}

void BitMEXHFTBot::place_order_with_retry(const std::string& side, int amount, double price, const std::string& symbol, const std::string& orderType) {
    // Pre-fetch order book and mark price data asynchronously
    auto orderBookFuture = std::async(std::launch::async, [this, &symbol]() {
        std::string endpoint = "/orderBook/L2?symbol=" + symbol + "&depth=1";
        return json::parse(http_request(endpoint, "GET"));
    });

    auto markPriceFuture = std::async(std::launch::async, [this, &symbol]() {
        std::string endpoint = "/instrument?symbol=" + symbol;
        return json::parse(http_request(endpoint, "GET"));
    });

    // Wait for both futures to complete
    json orderBookData = orderBookFuture.get();
    json instrumentData = markPriceFuture.get();

    // Process order book data
    double bestBid = 0.0, bestAsk = std::numeric_limits<double>::max();
    for (const auto& entry : orderBookData) {
        if (entry["side"] == "Buy") bestBid = std::max(bestBid, entry["price"].get<double>());
        else if (entry["side"] == "Sell") bestAsk = std::min(bestAsk, entry["price"].get<double>());
    }

    double markPrice = instrumentData[0]["markPrice"].get<double>();

    // Perform all checks in parallel
    auto fatFingerCheck = std::async(std::launch::async, [&]() {
        return TradingRules::checkFatFingerProtection(side, price, bestBid, bestAsk, markPrice);
    });

    auto orderLimitsCheck = std::async(std::launch::async, [&]() {
        int currentOpenOrders = 0, currentStopOrders = 0;
        for (const auto& [orderId, orderInfo] : open_orders) {
            if (orderInfo.symbol == symbol) {
                (orderInfo.ordType == "Limit" || orderInfo.ordType == "Market") ? ++currentOpenOrders : ++currentStopOrders;
            }
        }
        return TradingRules::checkOrderLimits(symbol, currentOpenOrders, currentStopOrders);
    });

    auto qvrCheck = std::async(std::launch::async, [&]() {
        double quoteValue = amount * price;
        return TradingRules::checkQVRThreshold(quoteValue, traded_value, quotes_submitted);
    });

    // Wait for all checks to complete
    if (!fatFingerCheck.get() || !orderLimitsCheck.get() || !qvrCheck.get()) {
        LOG_WARNING("Order rejected due to rule violation: " + side + " " + std::to_string(amount) + " @ " + std::to_string(price));
        return;
    }

    const int MAX_ATTEMPTS = 3;
    const std::chrono::milliseconds RATE_LIMIT_WAIT(100);
    const std::chrono::milliseconds LOAD_SHEDDING_WAIT(200);

    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        if (!rate_limiter->add_request()) {
            std::this_thread::sleep_for(RATE_LIMIT_WAIT);
            continue;
        }

        try {
            std::string endpoint = "/order";
            json order_data = {
                {"symbol", symbol},
                {"side", side},
                {"orderQty", amount},
                {"price", price},
                {"ordType", orderType}
            };
            std::string response = http_request(endpoint, "POST", order_data.dump());
            json result = json::parse(response);

            if (result.contains("orderID")) {
                std::string orderID = result["orderID"].get<std::string>();
                LOG_INFO(side + " order placed at " + std::to_string(price) + ", OrderID: " + orderID);
                open_orders[orderID] = {side, amount, price, symbol};
                std::async(std::launch::async, &BitMEXHFTBot::track_order, this, orderID);
                return;
            } else {
                throw std::runtime_error("Failed to place order: " + result["error"].get<std::string>());
            }
        } catch (const std::exception& e) {
            std::string error_message = e.what();
            if (error_message.find("Rate limit exceeded") != std::string::npos) {
                std::this_thread::sleep_for(RATE_LIMIT_WAIT);
            } else if (error_message.find("Load shedding") != std::string::npos) {
                std::this_thread::sleep_for(LOAD_SHEDDING_WAIT);
            } else {
                LOG_ERROR("Failed to place order: " + error_message);
                if (attempt == MAX_ATTEMPTS - 1) {
                    throw std::runtime_error("Failed to place order after " + std::to_string(MAX_ATTEMPTS) + " attempts: " + error_message);
                }
            }
        }
    }
}

void BitMEXHFTBot::cancel_all_orders(const std::string& side) {
    if (rate_limiter->add_request()) {
        std::string url = api_base_url + "/order/all";
        json cancel_data = {
            {"symbol", "XBTUSD"},
            {"side", side}
        };
        std::string response = http_request(url, "DELETE", cancel_data.dump());
        json result = json::parse(response);
        if (result.is_array()) {
            for (const auto& order : result) {
                if (order.contains("orderID")) {
                    std::string orderID = order["orderID"].get<std::string>();
                    open_orders.erase(orderID);
                    LOG_INFO("Order " + orderID + " cancelled");
                }
            }
        } else {
            throw std::runtime_error("Failed to cancel orders: " + result["error"].get<std::string>());
        }
    } else {
        throw std::runtime_error("Rate limit exceeded while trying to cancel orders");
    }
}

void BitMEXHFTBot::track_order(const std::string& orderID) {
    // Implement order tracking logic
    if (open_orders.find(orderID) != open_orders.end()) {
        // Update local data structure
        open_orders[orderID].status = "Tracking";
        open_orders[orderID].last_updated = std::chrono::system_clock::now();

        // Make API call to get order status
        if (rate_limiter->add_request()) {
            std::string endpoint = "/order?filter={\"orderID\":\"" + orderID + "\"}";
            std::string response = http_request(endpoint, "GET");
            json result = json::parse(response);

            if (!result.empty() && result[0].contains("ordStatus")) {
                std::string status = result[0]["ordStatus"].get<std::string>();
                open_orders[orderID].status = status;
                LOG_INFO("Order " + orderID + " status updated: " + status);
            } else {
                LOG_ERROR("Failed to get status for order: " + orderID);
            }
        } else {
            LOG_WARNING("Rate limit reached while tracking order: " + orderID);
        }
    } else {
        LOG_WARNING("Attempted to track unknown order: " + orderID);
    }
}

void BitMEXHFTBot::amend_order(const std::string& orderID, int new_amount, double new_price) {
    if (rate_limiter->add_request()) {
        std::string endpoint = "/order";
        json amend_data = {
            {"orderID", orderID},
            {"orderQty", new_amount},
            {"price", new_price}
        };
        std::string response = http_request(endpoint, "PUT", amend_data.dump());
        json result = json::parse(response);
        if (result.contains("orderID")) {
            LOG_INFO("Order " + orderID + " amended to " + std::to_string(new_amount) + " @ " + std::to_string(new_price));
            open_orders[orderID].amount = new_amount;
            open_orders[orderID].price = new_price;
        } else {
            LOG_ERROR("Failed to amend order: " + result["error"].get<std::string>());
            throw std::runtime_error("Failed to amend order: " + result["error"].get<std::string>());
        }
    } else {
        LOG_ERROR("Rate limit exceeded while trying to amend order");
        throw std::runtime_error("Rate limit exceeded while trying to amend order");
    }
}


void BitMEXHFTBot::manage_orders() {
    for (const auto& [orderID, order] : open_orders) {
        // Implement order management logic
        // This could involve checking order status, amending or cancelling based on market conditions
    }
}

void BitMEXHFTBot::cancel_order(const std::string& orderID) {
    if (rate_limiter->add_request()) {
        std::string url = "https://www.bitmex.com/api/v1/order";
        json cancel_data = {
            {"orderID", orderID}
        };
        std::string response = http_request(url, "DELETE", cancel_data.dump());
        json result = json::parse(response);
        if (result.contains("orderID")) {
            std::cout << "Order " << orderID << " cancelled" << std::endl;
            open_orders.erase(orderID);
        } else {
            throw std::runtime_error("Failed to cancel order: " + result["error"].get<std::string>());
        }
    } else {
        throw std::runtime_error("Rate limit exceeded while trying to cancel order");
    }
}

struct Order {
    std::string side;
    int amount;
    double price;
    std::string symbol;
};

std::unordered_map<std::string, Order> open_orders;

std::string BitMEXHFTBot::analyze_order_book() {
    std::lock_guard<std::mutex> lock(order_book_mutex);

    int bid_volume = 0, ask_volume = 0;
    double weighted_bid_price = 0, weighted_ask_price = 0;

    for (const auto& [price, bid] : order_book_bids) {
        bid_volume += bid.size;
        weighted_bid_price += price * bid.size;
    }

    for (const auto& [price, ask] : order_book_asks) {
        ask_volume += ask.size;
        weighted_ask_price += price * ask.size;
    }

    if (bid_volume == 0 || ask_volume == 0) {
        LOG_WARNING("Order book is empty or incomplete. Unable to analyze.");
        return "hold";
    }

    double avg_bid_price = weighted_bid_price / bid_volume;
    double avg_ask_price = weighted_ask_price / ask_volume;
    double mid_price = (avg_bid_price + avg_ask_price) / 2;

    // Get current position
    json position = get_position();
    double current_qty = position["currentQty"].get<double>();
    double avg_entry_price = position["avgEntryPrice"].get<double>();
    double unrealized_pnl = position["unrealisedPnl"].get<double>();

    // Calculate risk metrics
    double position_value = std::abs(current_qty) * mid_price;
    double leverage = position_value / current_balance;
    double pnl_percentage = unrealized_pnl / current_balance;

    // Implement sophisticated risk management
    if (leverage > Config::MAX_LEVERAGE) {
        return "reduce_position";
    }

    if (std::abs(pnl_percentage) > Config::MAX_PNL_PERCENTAGE) {
        return (pnl_percentage > 0) ? "take_profit" : "cut_loss";
    }

    // Analyze order book imbalance
    if (bid_volume > ask_volume * Config::VOLUME_IMBALANCE_THRESHOLD) {
        if (current_qty < 0) {
            return "close_short";
        } else if (current_qty >= 0 && leverage < Config::TARGET_LEVERAGE) {
            return "increase_long";
        }
    } else if (ask_volume > bid_volume * Config::VOLUME_IMBALANCE_THRESHOLD) {
        if (current_qty > 0) {
            return "close_long";
        } else if (current_qty <= 0 && leverage < Config::TARGET_LEVERAGE) {
            return "increase_short";
        }
    }

    // If no clear signal, maintain current position
    return "hold";
}

bool BitMEXHFTBot::check_stop_loss(double entry_price, double current_price, const std::string& side) {
    if (side == "Buy" && current_price <= entry_price * (1 - Config::STOP_LOSS_PERCENTAGE)) {
        return true;
    } else if (side == "Sell" && current_price >= entry_price * (1 + Config::STOP_LOSS_PERCENTAGE)) {
        return true;
    }
    return false;
}

bool BitMEXHFTBot::check_take_profit(double entry_price, double current_price, const std::string& side) {
    if (side == "Buy" && current_price >= entry_price * (1 + Config::TAKE_PROFIT_PERCENTAGE)) {
        return true;
    } else if (side == "Sell" && current_price <= entry_price * (1 - Config::TAKE_PROFIT_PERCENTAGE)) {
        return true;
    }
    return false;
}

void BitMEXHFTBot::set_dead_mans_switch(int timeout) {
    std::string url = "https://www.bitmex.com/api/v1/order/cancelAllAfter";
    json data = {{"timeout", timeout}};
    http_request(url, "POST", data.dump());
}

void BitMEXHFTBot::place_market_making_orders() {
    double current_price = get_market_price();
    double bid_price = current_price * (1 - Config::PRICE_OFFSET);
    double ask_price = current_price * (1 + Config::PRICE_OFFSET);
    int order_size = std::floor(trade_amount / current_price);

    place_order_with_retry("Buy", order_size, bid_price, "XBTUSD", "Limit");
    place_order_with_retry("Sell", order_size, ask_price, "XBTUSD", "Limit");
}
void BitMEXHFTBot::trade(const py::dict& market_data) {
    double mid_price = market_data["price"].cast<double>();
    double volume = market_data["volume"].cast<double>();
    double best_bid = market_data["best_bid"].cast<double>();
    double best_ask = market_data["best_ask"].cast<double>();
    
    try {
        if (!check_rate_limit()) {
            return;
        }

        LOG_INFO("Starting trading cycle");

        set_dead_mans_switch(Config::DEAD_MANS_SWITCH_TIMEOUT);
        
        if (!place_orders()) {
            return;
        }

        if (mid_price == 0) {
            return;
        }
        
        manage_positions(mid_price);

        update_trading_strategy(mid_price, best_bid, best_ask);

        adjust_orders_based_on_imbalance();

    } catch (const std::exception& e) {
        LOG_ERROR("An unexpected error occurred: " + std::string(e.what()));
        std::this_thread::sleep_for(std::chrono::seconds(Config::ERROR_WAIT_TIME));
    }
}

bool BitMEXHFTBot::check_rate_limit() {
    if (!rate_limiter->add_request()) {
        LOG_INFO("Rate limit exceeded. Adding request to queue.");
        request_queue.push(std::chrono::steady_clock::now());
        
        while (!request_queue.empty()) {
            auto oldest_request = request_queue.front();
            if (std::chrono::steady_clock::now() - oldest_request > rate_limiter->get_reset_interval()) {
                request_queue.pop();
                if (rate_limiter->add_request()) {
                    LOG_INFO("Processing queued request.");
                    return true;
                }
            } else {
                auto wait_time = rate_limiter->get_reset_interval() - (std::chrono::steady_clock::now() - oldest_request);
                LOG_INFO("Waiting " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(wait_time).count()) + "ms before retrying.");
                std::this_thread::sleep_for(wait_time);
            }
        }
        return false;
    }
    return true;
}

bool BitMEXHFTBot::place_orders() {
    try {
        place_market_making_orders();
        return true;
    } catch (const APIException& e) {
        LOG_ERROR("Error placing market making orders: " + std::string(e.what()));
        std::this_thread::sleep_for(std::chrono::seconds(Config::API_RETRY_DELAY));
        return false;
    }
}

double BitMEXHFTBot::get_current_price() {
    try {
        return get_market_price();
    } catch (const APIException& e) {
        LOG_ERROR("Error getting market price: " + std::string(e.what()));
        std::this_thread::sleep_for(std::chrono::seconds(Config::API_RETRY_DELAY));
        return 0;
    }
}

void BitMEXHFTBot::manage_positions(double current_price) {
    json positions;
    try {
        positions = get_positions();
    } catch (const APIException& e) {
        LOG_ERROR("Error getting positions: " + std::string(e.what()));
        std::this_thread::sleep_for(std::chrono::seconds(Config::API_RETRY_DELAY));
        return;
    }

    double total_risk = 0.0;
    double total_exposure = 0.0;
    double total_unrealized_pnl = 0.0;
    
    std::map<std::string, double> sector_exposure;

    for (const auto& position : positions) {
        double position_risk = calculate_position_risk(position, current_price);
        double position_exposure = std::abs(position["currentQty"].get<double>() * current_price);
        double unrealized_pnl = calculate_unrealized_pnl(position, current_price);
        std::string sector = get_sector(position["symbol"]);

        total_risk += position_risk;
        total_exposure += position_exposure;
        total_unrealized_pnl += unrealized_pnl;
        sector_exposure[sector] += position_exposure;
    }

    double max_portfolio_risk = Config::MAX_PORTFOLIO_RISK;
    double max_exposure = Config::MAX_PORTFOLIO_EXPOSURE;
    double max_sector_exposure = Config::MAX_SECTOR_EXPOSURE;

    if (total_risk > max_portfolio_risk) {
        LOG_WARNING("Total portfolio risk (" + std::to_string(total_risk) + ") exceeds maximum allowed risk (" + std::to_string(max_portfolio_risk) + ")");
        reduce_overall_risk(positions, current_price, total_risk, max_portfolio_risk);
    }

    if (total_exposure > max_exposure) {
        LOG_WARNING("Total portfolio exposure (" + std::to_string(total_exposure) + ") exceeds maximum allowed exposure (" + std::to_string(max_exposure) + ")");
        reduce_overall_exposure(positions, current_price, total_exposure, max_exposure);
    }

    for (const auto& [sector, exposure] : sector_exposure) {
        if (exposure > max_sector_exposure) {
            LOG_WARNING("Sector exposure for " + sector + " (" + std::to_string(exposure) + ") exceeds maximum allowed sector exposure (" + std::to_string(max_sector_exposure) + ")");
            reduce_sector_exposure(positions, current_price, sector, exposure, max_sector_exposure);
        }
    }

    double portfolio_beta = calculate_portfolio_beta(positions);
    if (std::abs(portfolio_beta - Config::TARGET_PORTFOLIO_BETA) > Config::BETA_TOLERANCE) {
        LOG_INFO("Adjusting portfolio beta from " + std::to_string(portfolio_beta) + " to target " + std::to_string(Config::TARGET_PORTFOLIO_BETA));
        adjust_portfolio_beta(positions, current_price, portfolio_beta, Config::TARGET_PORTFOLIO_BETA);
    }

    for (const auto& position : positions) {
        manage_single_position(position, current_price);
    }

    LOG_INFO("Portfolio risk: " + std::to_string(total_risk) + 
             ", Exposure: " + std::to_string(total_exposure) + 
             ", Unrealized PnL: " + std::to_string(total_unrealized_pnl) + 
             ", Beta: " + std::to_string(portfolio_beta));
}

void BitMEXHFTBot::manage_single_position(const json& position, double current_price) {
    std::string symbol = position["symbol"];
    double entry_price = position["avgEntryPrice"];
    int current_qty = position["currentQty"];
    std::string side = current_qty > 0 ? "Buy" : "Sell";

    LOG_INFO("Managing position for symbol: " + symbol + ", current quantity: " + std::to_string(current_qty));

    double unrealized_pnl = calculate_unrealized_pnl(position, current_price);
    double position_risk = calculate_position_risk(position, current_price);

    if (check_dynamic_stop_loss(entry_price, current_price, side, unrealized_pnl, position_risk)) {
        close_position(symbol, current_qty, side, "dynamic stop loss");
    } else if (check_dynamic_take_profit(entry_price, current_price, side, unrealized_pnl, position_risk)) {
        partial_close_position(symbol, current_qty, side, "dynamic take profit");
    } else if (check_trailing_stop(position, current_price)) {
        close_position(symbol, current_qty, side, "trailing stop");
    }

    adjust_position_size(symbol, current_qty, side, position_risk);
}

void BitMEXHFTBot::close_position(const std::string& symbol, int current_qty, const std::string& side, const std::string& reason) {
    try {
        place_order_with_retry(side == "Buy" ? "Sell" : "Buy", std::abs(current_qty), 0, symbol, "Market");
        LOG_INFO(reason + " triggered for " + symbol);
    } catch (const APIException& e) {
        LOG_ERROR("Error closing position (" + reason + ") for " + symbol + ": " + std::string(e.what()));
    }
}

void BitMEXHFTBot::partial_close_position(const std::string& symbol, int current_qty, const std::string& side, const std::string& reason) {
    int close_qty = std::abs(current_qty) / 2;  // Close half the position
    try {
        place_order_with_retry(side == "Buy" ? "Sell" : "Buy", close_qty, 0, symbol, "Market");
        LOG_INFO(reason + " triggered for " + symbol + ", partially closing position");
    } catch (const APIException& e) {
        LOG_ERROR("Error partially closing position (" + reason + ") for " + symbol + ": " + std::string(e.what()));
    }
}

double BitMEXHFTBot::calculate_unrealized_pnl(const json& position, double current_price) {
    double avg_entry_price = position["avgEntryPrice"].get<double>();
    double current_qty = position["currentQty"].get<double>();
    return (current_price - avg_entry_price) * current_qty;
}

std::string BitMEXHFTBot::get_sector(const std::string& symbol) {
    // This is a simplified implementation. In a real-world scenario, you'd have a more comprehensive mapping.
    if (symbol.substr(0, 2) == "XB") return "Crypto";
    if (symbol.substr(0, 2) == "ET") return "Equity";
    if (symbol.substr(0, 2) == "GC") return "Commodity";
    return "Other";
}

void BitMEXHFTBot::reduce_overall_risk(const json& positions, double current_price, double total_risk, double max_portfolio_risk) {
    double risk_reduction_needed = total_risk - max_portfolio_risk;
    for (const auto& position : positions) {
        double position_risk = calculate_position_risk(position, current_price);
        if (position_risk > 0) {
            double reduction_ratio = std::min(1.0, risk_reduction_needed / position_risk);
            int current_qty = position["currentQty"].get<int>();
            int reduce_qty = static_cast<int>(std::abs(current_qty) * reduction_ratio);
            if (reduce_qty > 0) {
                std::string symbol = position["symbol"];
                std::string side = current_qty > 0 ? "Sell" : "Buy";
                try {
                    place_order_with_retry(side, reduce_qty, 0, symbol, "Market");
                    LOG_INFO("Reduced position for " + symbol + " by " + std::to_string(reduce_qty) + " to decrease overall risk");
                } catch (const APIException& e) {
                    LOG_ERROR("Error reducing position for " + symbol + ": " + std::string(e.what()));
                }
            }
        }
    }
}

void BitMEXHFTBot::reduce_overall_exposure(const json& positions, double current_price, double total_exposure, double max_exposure) {
    double exposure_reduction_needed = total_exposure - max_exposure;
    for (const auto& position : positions) {
        double position_exposure = std::abs(position["currentQty"].get<double>() * current_price);
        if (position_exposure > 0) {
            double reduction_ratio = std::min(1.0, exposure_reduction_needed / position_exposure);
            int current_qty = position["currentQty"].get<int>();
            int reduce_qty = static_cast<int>(std::abs(current_qty) * reduction_ratio);
            if (reduce_qty > 0) {
                std::string symbol = position["symbol"];
                std::string side = current_qty > 0 ? "Sell" : "Buy";
                try {
                    place_order_with_retry(side, reduce_qty, 0, symbol, "Market");
                    LOG_INFO("Reduced position for " + symbol + " by " + std::to_string(reduce_qty) + " to decrease overall exposure");
                } catch (const APIException& e) {
                    LOG_ERROR("Error reducing position for " + symbol + ": " + std::string(e.what()));
                }
            }
        }
    }
}

void BitMEXHFTBot::reduce_sector_exposure(const json& positions, double current_price, const std::string& sector, double exposure, double max_sector_exposure) {
    double exposure_reduction_needed = exposure - max_sector_exposure;
    for (const auto& position : positions) {
        if (get_sector(position["symbol"]) == sector) {
            double position_exposure = std::abs(position["currentQty"].get<double>() * current_price);
            if (position_exposure > 0) {
                double reduction_ratio = std::min(1.0, exposure_reduction_needed / position_exposure);
                int current_qty = position["currentQty"].get<int>();
                int reduce_qty = static_cast<int>(std::abs(current_qty) * reduction_ratio);
                if (reduce_qty > 0) {
                    std::string symbol = position["symbol"];
                    std::string side = current_qty > 0 ? "Sell" : "Buy";
                    try {
                        place_order_with_retry(side, reduce_qty, 0, symbol, "Market");
                        LOG_INFO("Reduced position for " + symbol + " by " + std::to_string(reduce_qty) + " to decrease sector exposure");
                    } catch (const APIException& e) {
                        LOG_ERROR("Error reducing position for " + symbol + ": " + std::string(e.what()));
                    }
                }
            }
        }
    }
}

double BitMEXHFTBot::calculate_portfolio_beta(const json& positions) {
    double portfolio_beta = 0.0;
    double total_value = 0.0;
    for (const auto& position : positions) {
        double position_value = std::abs(position["currentQty"].get<double>() * position["lastPrice"].get<double>());
        double position_beta = 1.0; // Assuming a beta of 1 for simplicity. In reality, you'd need to calculate or lookup the beta for each instrument.
        portfolio_beta += position_beta * position_value;
        total_value += position_value;
    }
    return total_value > 0 ? portfolio_beta / total_value : 1.0;
}

void BitMEXHFTBot::adjust_portfolio_beta(const json& positions, double current_price, double current_beta, double target_beta) {
    double beta_adjustment = target_beta - current_beta;
    for (const auto& position : positions) {
        double position_beta = 1.0; // Assuming a beta of 1 for simplicity.
        if ((beta_adjustment > 0 && position_beta > 1.0) || (beta_adjustment < 0 && position_beta < 1.0)) {
            int current_qty = position["currentQty"].get<int>();
            int adjust_qty = static_cast<int>(std::abs(current_qty) * std::abs(beta_adjustment) * 0.1); // Adjust 10% of the position
            if (adjust_qty > 0) {
                std::string symbol = position["symbol"];
                std::string side = beta_adjustment > 0 ? "Buy" : "Sell";
                try {
                    place_order_with_retry(side, adjust_qty, 0, symbol, "Market");
                    LOG_INFO("Adjusted position for " + symbol + " by " + std::to_string(adjust_qty) + " to adjust portfolio beta");
                } catch (const APIException& e) {
                    LOG_ERROR("Error adjusting position for " + symbol + ": " + std::string(e.what()));
                }
            }
        }
    }
}

bool BitMEXHFTBot::check_dynamic_stop_loss(double entry_price, double current_price, const std::string& side, double unrealized_pnl, double position_risk) {
    double stop_loss_threshold = Config::STOP_LOSS_PERCENTAGE * position_risk;
    if (side == "Buy") {
        return current_price < entry_price && unrealized_pnl < -stop_loss_threshold;
    } else {
        return current_price > entry_price && unrealized_pnl < -stop_loss_threshold;
    }
}

size_t BitMEXHFTBot::curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    return newLength;
}

std::string BitMEXHFTBot::http_request(const std::string& endpoint, const std::string& method, const std::string& data) {
    const int MAX_RETRIES = 3;
    const std::chrono::seconds RETRY_DELAY(5);

    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        try {
            std::string response_string;
            std::string header_string;

            std::string api_key = std::getenv("API_KEY");
            std::string api_secret = std::getenv("API_SECRET");

            if (api_key.empty() || api_secret.empty()) {
                throw std::runtime_error("API credentials are missing or empty");
            }

            std::string url = Config::USE_TESTNET ? "https://testnet.bitmex.com" : "https://www.bitmex.com";
            url += "/api/v1" + endpoint;
            LOG_INFO("Making HTTP request to: " + url);

            if (!rate_limiter->add_request()) {
                LOG_WARNING("Rate limit exceeded. Waiting before retrying.");
                std::this_thread::sleep_for(std::chrono::seconds(5));
                return http_request(endpoint, method, data);  // Recursive call to retry
            }

            curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_write_callback);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_string);
            curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &header_string);
            curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 1L);

            if (method == "POST") {
                curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, data.c_str());
            } else if (method == "GET") {
                curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
            }

            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, ("api-key: " + api_key).c_str());

            long expires = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count() + 60;

            std::string sig_payload = method + "/api/v1" + endpoint + std::to_string(expires) + data;

            unsigned char* hmac = HMAC(EVP_sha256(), api_secret.c_str(), api_secret.length(),
                                       (unsigned char*)sig_payload.c_str(), sig_payload.length(), NULL, NULL);
            
            std::string signature = hex_encode(hmac, 32);

            headers = curl_slist_append(headers, ("api-expires: " + std::to_string(expires)).c_str());
            headers = curl_slist_append(headers, ("api-signature: " + signature).c_str());

            curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

            CURLcode res = curl_easy_perform(curl.get());
            if (res != CURLE_OK) {
                curl_slist_free_all(headers);
                throw APIException("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
            }

            long response_code;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);

            curl_slist_free_all(headers);

            if (response_code >= 400) {
                throw APIException("HTTP error: " + std::to_string(response_code) + " - " + response_string);
            }

            return response_string;
        } catch (const APIException& e) {
            LOG_ERROR("API error (attempt " + std::to_string(attempt + 1) + "): " + std::string(e.what()));
            if (attempt == MAX_RETRIES - 1) {
                throw;
            }
        } catch (const std::runtime_error& e) {
            LOG_ERROR("Runtime error (attempt " + std::to_string(attempt + 1) + "): " + std::string(e.what()));
            if (attempt == MAX_RETRIES - 1) {
                throw;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Unexpected error (attempt " + std::to_string(attempt + 1) + "): " + std::string(e.what()));
            if (attempt == MAX_RETRIES - 1) {
                throw;
            }
        }
        std::this_thread::sleep_for(RETRY_DELAY);
    }

    throw std::runtime_error("HTTP request failed after maximum retries");
}

void BitMEXHFTBot::update_order_book(const std::vector<OrderBookEntry>& bids, const std::vector<OrderBookEntry>& asks) {
    std::lock_guard<std::mutex> lock(order_book_mutex);

    order_book_bids.clear();
    order_book_asks.clear();

    for (const auto& bid : bids) {
        order_book_bids[bid.price] = bid;
    }

    for (const auto& ask : asks) {
        order_book_asks[ask.price] = ask;
    }

    // Maintain a maximum depth (e.g., 10 levels) to prevent memory issues
    const size_t max_depth = 10;
    while (order_book_bids.size() > max_depth) {
        order_book_bids.erase(order_book_bids.begin());
    }
    while (order_book_asks.size() > max_depth) {
        order_book_asks.erase(--order_book_asks.end());
    }

    LOG_INFO("Order book updated: " + std::to_string(order_book_bids.size()) + " bids, " + std::to_string(order_book_asks.size()) + " asks");
}


bool BitMEXHFTBot::is_bearish_trend(double lastPrice, double markPrice, double fundingRate) {
    std::vector<double> prices = getHistoricalPrices(20);
    double sma20 = std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
    return lastPrice < sma20 && fundingRate > 0;
}

double BitMEXHFTBot::calculate_position_risk(const json& position, double current_price) {
    double currentQty = position["currentQty"].get<double>();
    double avgEntryPrice = position["avgEntryPrice"].get<double>();
    double unrealizedPnl = currentQty * (current_price - avgEntryPrice);
    double positionValue = std::abs(currentQty * current_price);
    return std::abs(unrealizedPnl) / positionValue;
}

template<typename PQ>
void remove_from_priority_queue(PQ& pq, const OrderBookEntry& obe) {
    std::vector<OrderBookEntry> temp;
    while (!pq.empty() && pq.top().id != obe.id) {
        temp.push_back(pq.top());
        pq.pop();
    }
    if (!pq.empty()) pq.pop();
    for (const auto& entry : temp) {
        pq.push(entry);
    }
}

template<typename PQ>
void update_priority_queue(PQ& pq, const OrderBookEntry& obe) {
    remove_from_priority_queue(pq, obe);
    pq.push(obe);
}

void BitMEXHFTBot::subscribe_to_order_book() {
    json subscription = {
        {"op", "subscribe"},
        {"args", {"orderBookL2:XBTUSD"}}
    };
    ws_connection->send(subscription.dump());
    LOG_INFO("Subscribed to orderBookL2 WebSocket feed");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Add a small delay
}
        // Analyze order book imbalance and adjust orders


void BitMEXHFTBot::adjust_orders_based_on_imbalance() {
    std::lock_guard<std::mutex> lock(order_book_mutex);
    
    // Calculate total volume for bids and asks
    double total_bid_volume = 0.0;
    double total_ask_volume = 0.0;
    
    for (const auto& [price, bid] : order_book_bids) {
        total_bid_volume += bid.size;
    }
    
    for (const auto& [price, ask] : order_book_asks) {
        total_ask_volume += ask.size;
    }
    
    // Calculate imbalance ratio
    double imbalance_ratio = total_bid_volume / (total_bid_volume + total_ask_volume);
    
    // Use configurable thresholds
    double buy_threshold = Config::BUY_IMBALANCE_THRESHOLD;
    double sell_threshold = Config::SELL_IMBALANCE_THRESHOLD;
    
    // Adjust orders based on imbalance
    if (imbalance_ratio > buy_threshold) {  // More buying pressure
        // Cancel existing sell orders
        cancel_all_orders("Sell");
        
        // Place new buy order
        if (!order_book_asks.empty()) {
            double best_ask = order_book_asks.begin()->first;
            place_order_with_retry("Buy", Config::ORDER_SIZE, best_ask * 0.9999, "XBTUSD", "Limit");
        }
        
        LOG_INFO("Adjusted orders due to buying pressure. Imbalance ratio: " + std::to_string(imbalance_ratio));
    } else if (imbalance_ratio < sell_threshold) {  // More selling pressure
        // Cancel existing buy orders
        cancel_all_orders("Buy");
        
        // Place new sell order
        if (!order_book_bids.empty()) {
            double best_bid = order_book_bids.rbegin()->first;
            place_order_with_retry("Sell", Config::ORDER_SIZE, best_bid * 1.0001, "XBTUSD", "Limit");
        }
        
        LOG_INFO("Adjusted orders due to selling pressure. Imbalance ratio: " + std::to_string(imbalance_ratio));
    } else {
        LOG_INFO("Order book is balanced. No adjustment needed. Imbalance ratio: " + std::to_string(imbalance_ratio));
    }
}

bool BitMEXHFTBot::is_overbought() {
    std::vector<double> prices = getHistoricalPrices(14);
    double rsi = indicators.calculateRSI(prices);
    return rsi > 70;
}

bool BitMEXHFTBot::is_oversold() {
    std::vector<double> prices = getHistoricalPrices(14);
    double rsi = indicators.calculateRSI(prices);
    return rsi < 30;
}

void BitMEXHFTBot::reducePosition(double currentQty, double markPrice, const std::string& direction) {
    double reduceAmount = std::abs(currentQty) * 0.1; // Reduce by 10%
    std::string side = (direction == "long") ? "Sell" : "Buy";
    place_order_with_retry(side, reduceAmount, markPrice, "XBTUSD", "Market");
}

std::vector<double> BitMEXHFTBot::getHistoricalPrices(int period) {
    std::string endpoint = "/trade/bucketed?binSize=1h&partial=false&count=" + std::to_string(period) + "&symbol=XBTUSD";
    std::string response = http_request(endpoint, "GET");
    json result = json::parse(response);
    
    std::vector<double> prices;
    for (const auto& candle : result) {
        prices.push_back(candle["close"].get<double>());
    }
    return prices;
}

void BitMEXHFTBot::closePosition(double currentQty, double markPrice, const std::string& direction) {
    std::string side = (direction == "long") ? "Sell" : "Buy";
    place_order_with_retry(side, std::abs(currentQty), markPrice, "XBTUSD", "Market");
}

void BitMEXHFTBot::partialClosePosition(double currentQty, double markPrice, const std::string& direction) {
    double closeAmount = std::abs(currentQty) * 0.5; // Close 50% of the position
    std::string side = (direction == "long") ? "Sell" : "Buy";
    place_order_with_retry(side, closeAmount, markPrice, "XBTUSD", "Market");
}

void BitMEXHFTBot::adjustOrders(double lastPrice, double markPrice) {
    cancel_all_orders("");
    double spread = Config::ORDER_SPREAD;
    place_order_with_retry("Buy", Config::ORDER_SIZE, lastPrice * (1 - spread), "XBTUSD", "Limit");
    place_order_with_retry("Sell", Config::ORDER_SIZE, lastPrice * (1 + spread), "XBTUSD", "Limit");
}

bool BitMEXHFTBot::is_bullish_trend(double lastPrice, double markPrice, double fundingRate) {
    std::vector<double> prices = getHistoricalPrices(20);
    double sma20 = std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
    return lastPrice > sma20 && fundingRate < 0;
}

double BitMEXHFTBot::calculatePositionSize(double lastPrice) {
    double accountBalance = get_account_balance();
    double riskPerTrade = accountBalance * Config::RISK_PER_TRADE;
    return riskPerTrade / lastPrice;
}

void BitMEXHFTBot::openNewPosition(const std::string& direction, double size) {
    std::string side = (direction == "long") ? "Buy" : "Sell";
    double currentPrice = get_current_price();
    place_order_with_retry(side, size, currentPrice, "XBTUSD", "Market");
}
void BitMEXHFTBot::start_heartbeat() {
    std::thread([this]() {
        while (true) {
            if (ws_connection && ws_connection->get_state() == websocketpp::session::state::open) {
                ws_connection->ping("");
            }
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }).detach();
}

bool BitMEXHFTBot::check_trailing_stop(const json& position, double current_price) {
    double entry_price = position["avgEntryPrice"].get<double>();
    double current_qty = position["currentQty"].get<double>();
    double trailing_stop = position["trailingStop"].get<double>();
    
    if (current_qty > 0) {
        return current_price <= trailing_stop;
    } else if (current_qty < 0) {
        return current_price >= trailing_stop;
    }
    return false;
}

double BitMEXHFTBot::get_account_balance() {
    if (rate_limiter->add_request()) {
        std::string endpoint = "/user/margin?currency=XBt";
        std::string response = http_request(endpoint, "GET");
        json result = json::parse(response);
        return result["marginBalance"].get<double>() / 100000000.0; // Convert satoshis to BTC
    }
    throw std::runtime_error("Rate limit exceeded while trying to get account balance");
}

std::vector<py::dict> BitMEXHFTBot::get_desired_orders() {
    std::vector<py::dict> orders;
    
    // Get current market data
    double current_price = get_current_price();
    double account_balance = get_account_balance();
    
    // Check if we should open a new position
    if (should_open_position(current_price)) {
        double position_size = calculatePositionSize(current_price);
        std::string direction = (current_price > last_sma20) ? "long" : "short";
        std::string side = (direction == "long") ? "Buy" : "Sell";
        
        // Create a new order
        orders.push_back(py::dict(
            "side"_a=side,
            "id"_a=order_id_counter++,
            "price"_a=current_price,
            "quantity"_a=position_size
        ));
    }
    
    // Check existing positions and adjust if necessary
    json positions = get_positions();
    for (const auto& position : positions) {
        std::string symbol = position["symbol"].get<std::string>();
        int current_qty = position["currentQty"].get<int>();
        std::string side = (current_qty > 0) ? "Buy" : "Sell";
        double entry_price = position["avgEntryPrice"].get<double>();
        double unrealized_pnl = position["unrealisedPnl"].get<double>();
        double position_risk = std::abs(current_qty * entry_price / account_balance);
        
        // Check trailing stop
        if (check_trailing_stop(position, current_price)) {
            orders.push_back(py::dict(
                "side"_a=(side == "Buy" ? "Sell" : "Buy"),
                "id"_a=order_id_counter++,
                "price"_a=current_price,
                "quantity"_a=std::abs(current_qty)
            ));
        }
        
        // Check dynamic take profit
        if (check_dynamic_take_profit(entry_price, current_price, side, unrealized_pnl, position_risk)) {
            orders.push_back(py::dict(
                "side"_a=(side == "Buy" ? "Sell" : "Buy"),
                "id"_a=order_id_counter++,
                "price"_a=current_price,
                "quantity"_a=std::abs(current_qty)
            ));
        }
        
        // Adjust position size if necessary
        double target_risk = Config::RISK_PER_TRADE;
        if (position_risk > target_risk) {
            int reduce_qty = static_cast<int>(std::abs(current_qty) * (position_risk - target_risk) / position_risk);
            if (reduce_qty > 0) {
                orders.push_back(py::dict(
                    "side"_a=(side == "Buy" ? "Sell" : "Buy"),
                    "id"_a=order_id_counter++,
                    "price"_a=current_price,
                    "quantity"_a=reduce_qty
                ));
            }
        }
    }
    
    return orders;
}


void BitMEXHFTBot::adjust_position_size(const std::string& symbol, int current_qty, const std::string& side, double position_risk) {
    double target_risk = Config::RISK_PER_TRADE;
    if (position_risk > target_risk) {
        int reduce_qty = static_cast<int>(std::abs(current_qty) * (position_risk - target_risk) / position_risk);
        if (reduce_qty > 0) {
            place_order_with_retry(side == "Buy" ? "Sell" : "Buy", reduce_qty, 0, symbol, "Market");
        }
    }
}

bool BitMEXHFTBot::check_dynamic_take_profit(double entry_price, double current_price, const std::string& side, double unrealized_pnl, double position_risk) {
    double take_profit_threshold = Config::TAKE_PROFIT_PERCENTAGE * position_risk;
    if (side == "Buy") {
        return current_price > entry_price && unrealized_pnl > take_profit_threshold;
    } else {
        return current_price < entry_price && unrealized_pnl > take_profit_threshold;
    }
}