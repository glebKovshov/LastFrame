#include "core/RateLimiter.h"

#include <stdexcept>

namespace LastFrame::Core {

RateLimiter::RateLimiter(const std::size_t maxEvents, const std::chrono::milliseconds window,
                         const std::chrono::milliseconds cooldown)
    : maxEvents_(maxEvents), window_(window), cooldown_(cooldown) {
    if (maxEvents == 0 || window <= std::chrono::milliseconds::zero() ||
        cooldown <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("RateLimiter arguments must be positive");
    }
}

RateLimitResult RateLimiter::tryAccept(const std::chrono::steady_clock::time_point now) {
    if (now < cooldownUntil_) {
        return RateLimitResult::Cooldown;
    }

    while (!events_.empty() && now - events_.front() >= window_) {
        events_.pop_front();
    }

    if (events_.size() >= maxEvents_) {
        cooldownUntil_ = now + cooldown_;
        return RateLimitResult::Cooldown;
    }

    events_.push_back(now);
    return RateLimitResult::Accepted;
}

bool RateLimiter::inCooldown(const std::chrono::steady_clock::time_point now) const {
    return now < cooldownUntil_;
}

} // namespace LastFrame::Core
