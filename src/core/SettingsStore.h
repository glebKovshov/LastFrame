#pragma once

#include "core/Settings.h"

#include <filesystem>

namespace LastFrame::Core {

class SettingsStore final {
public:
    explicit SettingsStore(std::filesystem::path path);

    [[nodiscard]] Settings load(bool* recovered = nullptr) const;
    void save(const Settings& settings) const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace LastFrame::Core
