#include "core/SettingsStore.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace LastFrame::Core {
namespace {

std::string timestampForFile() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    std::ostringstream result;
    result << std::put_time(&localTime, "%Y%m%d%H%M%S");
    return result.str();
}

} // namespace

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

Settings SettingsStore::load(bool* recovered) const {
    if (recovered != nullptr) {
        *recovered = false;
    }

    if (!std::filesystem::exists(path_)) {
        return Settings::defaults();
    }

    try {
        std::ifstream input(path_, std::ios::binary);
        if (!input) {
            throw std::runtime_error("settings file cannot be opened");
        }
        const std::string content((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
        input.close();
        const auto first = content.find_first_not_of(" \t\r\n");
        const auto last = content.find_last_not_of(" \t\r\n");
        if (first == std::string::npos || content.at(first) != '{' || content.at(last) != '}') {
            throw std::runtime_error("settings root is not a JSON object");
        }
        const nlohmann::json json = nlohmann::json::parse(content);
        return Settings::fromJson(json);
    } catch (...) {
        const auto backup = path_.string() + ".broken-" + timestampForFile();
        std::error_code error;
        std::filesystem::rename(path_, backup, error);
        if (error) {
            // Some Windows filesystem providers reject rename while the file
            // was recently opened. Preserve the recovery contract with a
            // copy-then-remove fallback; the invalid file is never silently
            // discarded.
            error.clear();
            std::filesystem::copy_file(path_, backup, std::filesystem::copy_options::overwrite_existing, error);
            if (!error) {
                std::filesystem::remove(path_, error);
            }
        }
        if (recovered != nullptr) {
            *recovered = true;
        }
        return Settings::defaults();
    }
}

void SettingsStore::save(const Settings& settings) const {
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const auto temporaryPath = path_.string() + ".tmp";
    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("settings temporary file cannot be opened");
        }
        output << settings.toJson().dump(2) << '\n';
        output.flush();
        if (!output) {
            throw std::runtime_error("settings temporary file cannot be written");
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(std::filesystem::path(temporaryPath).c_str(), path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("settings atomic rename failed");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporaryPath, path_, error);
    if (error) {
        throw std::runtime_error("settings atomic rename failed");
    }
#endif
}

} // namespace LastFrame::Core
