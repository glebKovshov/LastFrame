#pragma once

#include <chrono>
#include <cstddef>
#include <deque>

namespace LastFrame::Core {

enum class RateLimitResult {
    Accepted,
    Cooldown,
};

class RateLimiter final {
public:
    RateLimiter(std::size_t maxEvents, std::chrono::milliseconds window,
                std::chrono::milliseconds cooldown);

    [[nodiscard]] RateLimitResult tryAccept(std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool inCooldown(std::chrono::steady_clock::time_point now) const;
    [[nodiscard]] std::size_t eventCount() const noexcept { return events_.size(); }

private:
    const std::size_t maxEvents_;
    const std::chrono::milliseconds window_;
    const std::chrono::milliseconds cooldown_;
    std::deque<std::chrono::steady_clock::time_point> events_;
    std::chrono::steady_clock::time_point cooldownUntil_{};
};

} // namespace LastFrame::Core
