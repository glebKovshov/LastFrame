#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace LastFrame::Core {

template <typename T>
class BoundedQueue final {
public:
    explicit BoundedQueue(const std::size_t capacity) : capacity_(capacity) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    [[nodiscard]] bool push(T value) {
        std::lock_guard lock(mutex_);
        if (closed_ || queue_.size() >= capacity_) {
            ++rejectedCount_;
            return false;
        }
        queue_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> tryPop() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    [[nodiscard]] std::optional<T> waitPop() {
        std::unique_lock lock(mutex_);
        notEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] std::size_t rejectedCount() const {
        std::lock_guard lock(mutex_);
        return rejectedCount_;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::deque<T> queue_;
    std::size_t rejectedCount_ = 0;
    bool closed_ = false;
};

} // namespace LastFrame::Core
