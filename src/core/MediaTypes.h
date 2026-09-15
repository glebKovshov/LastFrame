#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace LastFrame::Core {

using Timestamp = std::int64_t;

struct EncodedSegment {
    std::string id;
    Timestamp startPts = 0;
    Timestamp endPts = 0;
    bool startsWithKeyframe = false;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;

    [[nodiscard]] bool isValid() const noexcept {
        return !id.empty() && endPts > startPts && startsWithKeyframe;
    }
};

struct BufferSnapshot {
    Timestamp requestedStartPts = 0;
    Timestamp actualStartPts = 0;
    Timestamp endPts = 0;
    std::vector<std::shared_ptr<const EncodedSegment>> segments;

    [[nodiscard]] Timestamp duration() const noexcept {
        return endPts > actualStartPts ? endPts - actualStartPts : 0;
    }

    [[nodiscard]] bool empty() const noexcept { return segments.empty(); }
};

} // namespace LastFrame::Core
