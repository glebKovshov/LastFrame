#include "core/FilenameAllocator.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace LastFrame::Core {

std::filesystem::path FilenameAllocator::allocate(
    const std::filesystem::path& directory,
    const std::chrono::system_clock::time_point timestamp,
    std::string extension) {
    if (extension.empty()) {
        throw std::invalid_argument("Filename extension must not be empty");
    }
    if (extension.front() == '.') {
        extension.erase(extension.begin());
    }

    const std::string base = "LastFrame_" + formatTimestamp(timestamp);
    std::filesystem::path candidate = directory / (base + "." + extension);
    for (int suffix = 1; std::filesystem::exists(candidate); ++suffix) {
        candidate = directory / (base + "(" + std::to_string(suffix) + ")." + extension);
    }
    return candidate;
}

std::string FilenameAllocator::formatTimestamp(
    const std::chrono::system_clock::time_point timestamp) {
    const std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S");
    return stream.str();
}

} // namespace LastFrame::Core
