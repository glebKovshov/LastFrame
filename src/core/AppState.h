#pragma once

#include <string_view>

namespace LastFrame::Core {

enum class AppState {
    Idle,
    Starting,
    Recording,
    Paused,
    Stopped,
    Exporting,
    Reconfigure,
    Error,
};

[[nodiscard]] std::string_view appStateName(AppState state) noexcept;
[[nodiscard]] bool canTransition(AppState from, AppState to) noexcept;

class StateMachine final {
public:
    explicit StateMachine(AppState initialState = AppState::Idle) noexcept;

    [[nodiscard]] AppState state() const noexcept { return state_; }
    [[nodiscard]] bool transition(AppState next) noexcept;

private:
    AppState state_;
};

} // namespace LastFrame::Core
