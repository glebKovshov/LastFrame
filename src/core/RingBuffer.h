#pragma once

#include "core/MediaTypes.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace LastFrame::Core {

class RingBuffer final {
public:
    explicit RingBuffer(Timestamp durationUs);

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    void setDuration(Timestamp durationUs);
    [[nodiscard]] Timestamp duration() const;

    void append(std::shared_ptr<const EncodedSegment> segment);
    [[nodiscard]] BufferSnapshot snapshot(Timestamp nowPts) const;
    void clear();

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] Timestamp oldestPts() const;
    [[nodiscard]] Timestamp newestPts() const;

private:
    void evictLocked(Timestamp nowPts);

    mutable std::mutex mutex_;
    Timestamp durationUs_;
    std::deque<std::shared_ptr<const EncodedSegment>> segments_;
};

} // namespace LastFrame::Core
