#include "core/AppState.h"

namespace LastFrame::Core {

std::string_view appStateName(const AppState state) noexcept {
    switch (state) {
    case AppState::Idle: return "Idle";
    case AppState::Starting: return "Starting";
    case AppState::Recording: return "Recording";
    case AppState::Paused: return "Paused";
    case AppState::Stopped: return "Stopped";
    case AppState::Exporting: return "Exporting";
    case AppState::Reconfigure: return "Reconfigure";
    case AppState::Error: return "Error";
    }
    return "Unknown";
}

bool canTransition(const AppState from, const AppState to) noexcept {
    if (from == to) {
        return true;
    }

    switch (from) {
    case AppState::Idle:
        return to == AppState::Starting || to == AppState::Error;
    case AppState::Starting:
        return to == AppState::Recording || to == AppState::Error || to == AppState::Idle;
    case AppState::Recording:
        return to == AppState::Paused || to == AppState::Stopped || to == AppState::Exporting ||
               to == AppState::Reconfigure || to == AppState::Error;
    case AppState::Paused:
        return to == AppState::Recording || to == AppState::Stopped || to == AppState::Exporting ||
               to == AppState::Error;
    case AppState::Stopped:
        return to == AppState::Starting || to == AppState::Idle || to == AppState::Error;
    case AppState::Exporting:
        return to == AppState::Recording || to == AppState::Paused || to == AppState::Error;
    case AppState::Reconfigure:
        return to == AppState::Idle || to == AppState::Starting || to == AppState::Error;
    case AppState::Error:
        return to == AppState::Idle || to == AppState::Starting;
    }
    return false;
}

StateMachine::StateMachine(const AppState initialState) noexcept : state_(initialState) {}

bool StateMachine::transition(const AppState next) noexcept {
    if (!canTransition(state_, next)) {
        return false;
    }
    state_ = next;
    return true;
}

} // namespace LastFrame::Core
