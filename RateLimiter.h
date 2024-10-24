#pragma once
#include <vector>
#include <chrono>
#include <array>

class RateLimiter {
public:
    static constexpr int NUM_LIMITS = 2;

    RateLimiter();
    RateLimiter(std::array<int, NUM_LIMITS> max_requests, std::array<std::chrono::seconds, NUM_LIMITS> time_frames);
    bool add_request(bool is_order_route = false);
    std::chrono::seconds get_reset_interval() const { return time_frames[0]; }

private:
    std::array<int, NUM_LIMITS> max_requests{120, 10};
    std::array<std::chrono::seconds, NUM_LIMITS> time_frames{std::chrono::seconds(60), std::chrono::seconds(1)};
    std::array<std::vector<std::chrono::steady_clock::time_point>, NUM_LIMITS> requests;

    bool check_limit(int limit_index);
    void cleanup_old_requests(int limit_index);
};