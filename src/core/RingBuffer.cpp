#include "core/RingBuffer.h"

#include <algorithm>
#include <stdexcept>

namespace LastFrame::Core {

RingBuffer::RingBuffer(const Timestamp durationUs) : durationUs_(durationUs) {
    if (durationUs <= 0) {
        throw std::invalid_argument("RingBuffer duration must be positive");
    }
}

void RingBuffer::setDuration(const Timestamp durationUs) {
    if (durationUs <= 0) {
        throw std::invalid_argument("RingBuffer duration must be positive");
    }
    std::lock_guard lock(mutex_);
    durationUs_ = durationUs;
    if (!segments_.empty()) {
        evictLocked(segments_.back()->endPts);
    }
}

Timestamp RingBuffer::duration() const {
    std::lock_guard lock(mutex_);
    return durationUs_;
}

void RingBuffer::append(std::shared_ptr<const EncodedSegment> segment) {
    if (!segment || !segment->isValid()) {
        throw std::invalid_argument("RingBuffer accepts only valid segments");
    }

    std::lock_guard lock(mutex_);
    if (!segments_.empty() && segment->startPts < segments_.back()->startPts) {
        throw std::invalid_argument("RingBuffer segments must be appended in timestamp order");
    }
    segments_.push_back(std::move(segment));
    evictLocked(segments_.back()->endPts);
}

BufferSnapshot RingBuffer::snapshot(const Timestamp nowPts) const {
    std::lock_guard lock(mutex_);
    BufferSnapshot result;
    if (segments_.empty()) {
        return result;
    }

    result.endPts = std::min(nowPts, segments_.back()->endPts);
    result.requestedStartPts = result.endPts - durationUs_;

    auto first = segments_.end();
    for (auto it = segments_.begin(); it != segments_.end(); ++it) {
        if ((*it)->startPts <= result.requestedStartPts && (*it)->endPts > result.requestedStartPts) {
            first = it;
            break;
        }
        if ((*it)->startPts > result.requestedStartPts) {
            first = it;
            break;
        }
    }
    if (first == segments_.end()) {
        first = segments_.begin();
    }

    result.actualStartPts = (*first)->startPts;
    for (auto it = first; it != segments_.end() && (*it)->startPts < result.endPts; ++it) {
        result.segments.push_back(*it);
    }
    return result;
}

void RingBuffer::clear() {
    std::lock_guard lock(mutex_);
    segments_.clear();
}

std::size_t RingBuffer::size() const {
    std::lock_guard lock(mutex_);
    return segments_.size();
}

Timestamp RingBuffer::oldestPts() const {
    std::lock_guard lock(mutex_);
    return segments_.empty() ? 0 : segments_.front()->startPts;
}

Timestamp RingBuffer::newestPts() const {
    std::lock_guard lock(mutex_);
    return segments_.empty() ? 0 : segments_.back()->endPts;
}

void RingBuffer::evictLocked(const Timestamp nowPts) {
    const Timestamp cutoff = nowPts - durationUs_;
    while (!segments_.empty() && segments_.front()->endPts <= cutoff) {
        segments_.pop_front();
    }
}

} // namespace LastFrame::Core
