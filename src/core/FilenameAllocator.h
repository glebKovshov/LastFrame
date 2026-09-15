#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace LastFrame::Core {

class FilenameAllocator final {
public:
    [[nodiscard]] static std::filesystem::path allocate(
        const std::filesystem::path& directory,
        std::chrono::system_clock::time_point timestamp,
        std::string extension);

private:
    [[nodiscard]] static std::string formatTimestamp(
        std::chrono::system_clock::time_point timestamp);
};

} // namespace LastFrame::Core
