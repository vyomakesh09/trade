#include "RateLimiter.h"
#include <algorithm>

RateLimiter::RateLimiter(std::array<int, NUM_LIMITS> max_requests, std::array<std::chrono::seconds, NUM_LIMITS> time_frames)
    : max_requests(max_requests), time_frames(time_frames) {}
    
bool RateLimiter::add_request(bool is_order_route) {
    auto current_time = std::chrono::steady_clock::now();

    for (size_t i = 0; i < NUM_LIMITS; ++i) {
        requests[i].erase(
            std::remove_if(requests[i].begin(), requests[i].end(),
                [&](const auto& req) {
                    return std::chrono::duration_cast<std::chrono::seconds>(current_time - req).count() >= time_frames[i].count();
                }),
            requests[i].end()
        );

        if (requests[i].size() < static_cast<size_t>(max_requests[i])) {
            requests[i].push_back(current_time);
            return true;
        }
    }

    return false;
}