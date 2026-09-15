#include "core/AppState.h"
#include "core/BoundedQueue.h"
#include "core/FilenameAllocator.h"
#include "core/RateLimiter.h"
#include "core/RingBuffer.h"
#include "core/SettingsStore.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace LastFrame::Core;

namespace {

std::shared_ptr<const EncodedSegment> segment(std::string id, Timestamp start, Timestamp end) {
    auto value = std::make_shared<EncodedSegment>();
    value->id = std::move(id);
    value->startPts = start;
    value->endPts = end;
    value->startsWithKeyframe = true;
    return value;
}

void testRingBuffer() {
    RingBuffer buffer(3'000'000);
    buffer.append(segment("a", 0, 1'000'000));
    buffer.append(segment("b", 1'000'000, 2'000'000));
    buffer.append(segment("c", 2'000'000, 3'000'000));
    buffer.append(segment("d", 3'000'000, 4'000'000));
    assert(buffer.size() == 3);
    const auto snapshot = buffer.snapshot(4'000'000);
    assert(snapshot.segments.size() == 3);
    assert(snapshot.segments.front()->id == "b");
    assert(snapshot.duration() == 3'000'000);
}

void testRateLimiter() {
    using namespace std::chrono_literals;
    RateLimiter limiter(3, 1s, 5s);
    const auto start = std::chrono::steady_clock::now();
    assert(limiter.tryAccept(start) == RateLimitResult::Accepted);
    assert(limiter.tryAccept(start + 10ms) == RateLimitResult::Accepted);
    assert(limiter.tryAccept(start + 20ms) == RateLimitResult::Accepted);
    assert(limiter.tryAccept(start + 30ms) == RateLimitResult::Cooldown);
    assert(limiter.tryAccept(start + 4s) == RateLimitResult::Cooldown);
    assert(limiter.tryAccept(start + 5s + 31ms) == RateLimitResult::Accepted);
}

void testStateMachine() {
    StateMachine state;
    assert(state.transition(AppState::Starting));
    assert(state.transition(AppState::Recording));
    assert(state.transition(AppState::Paused));
    assert(!state.transition(AppState::Idle));
    assert(state.transition(AppState::Stopped));
    assert(state.transition(AppState::Starting));
}

void testQueue() {
    BoundedQueue<int> queue(2);
    assert(queue.push(1));
    assert(queue.push(2));
    assert(!queue.push(3));
    assert(queue.rejectedCount() == 1);
    assert(queue.tryPop().value() == 1);
    assert(queue.tryPop().value() == 2);
    assert(!queue.tryPop().has_value());
}

void testFilenameAllocator() {
    const auto directory = std::filesystem::temp_directory_path() / "lastframe-core-tests";
    std::filesystem::create_directories(directory);
    const auto timestamp = std::chrono::system_clock::from_time_t(0);
    const auto first = FilenameAllocator::allocate(directory, timestamp, ".mp4");
    std::ofstream(first).put('x');
    const auto second = FilenameAllocator::allocate(directory, timestamp, "mp4");
    assert(first.filename().string() != second.filename().string());
    std::filesystem::remove(first);
    std::filesystem::remove_all(directory);
}

void testSettings() {
    const auto uniqueName = std::string("lastframe-settings-tests-") +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / uniqueName / "settings.json";
    SettingsStore store(path);

    Settings settings = Settings::defaults();
    settings.buffer.durationSeconds = 45;
    settings.extras["futureFlag"] = true;
    store.save(settings);
    const auto loaded = store.load();
    assert(loaded.buffer.durationSeconds == 45);
    assert(loaded.extras.at("futureFlag") == true);

    {
        std::ofstream broken(path, std::ios::trunc);
        broken << "{broken";
    }
    bool recovered = false;
    const auto recoveredSettings = store.load(&recovered);
    assert(recovered);
    assert(recoveredSettings.buffer.durationSeconds == 30);
}

} // namespace

int main() {
    testRingBuffer();
    testRateLimiter();
    testStateMachine();
    testQueue();
    testFilenameAllocator();
    testSettings();
    std::cout << "LastFrame core tests passed\n";
}
