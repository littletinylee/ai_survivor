#include "core/game_state.h"

namespace ai_survivors {

const char *to_string(GameState state) {
    switch (state) {
        case GameState::kMenu:
            return "Menu";
        case GameState::kPlaying:
            return "Playing";
        case GameState::kPaused:
            return "Paused";
        case GameState::kUpgrade:
            return "Upgrade";
        case GameState::kWon:
            return "Won";
        case GameState::kLost:
            return "Lost";
    }
    return "Unknown";
}

bool GameStateManager::is_terminal() const {
    return state_ == GameState::kWon || state_ == GameState::kLost;
}

bool GameStateManager::is_time_running() const {
    return state_ == GameState::kPlaying;
}

bool GameStateManager::is_valid_transition(GameState from, GameState to) {
    if (from == to) {
        return false;
    }

    switch (from) {
        case GameState::kMenu:
            return to == GameState::kPlaying;
        case GameState::kPlaying:
            return to == GameState::kPaused || to == GameState::kUpgrade ||
                   to == GameState::kWon || to == GameState::kLost;
        case GameState::kPaused:
            return to == GameState::kPlaying;
        case GameState::kUpgrade:
            return to == GameState::kPlaying;
        case GameState::kWon:
        case GameState::kLost:
            return false;
    }
    return false;
}

bool GameStateManager::transition_to(GameState next) {
    if (!is_valid_transition(state_, next)) {
        return false;
    }
    state_ = next;
    return true;
}

bool GameStateManager::start_run() {
    return transition_to(GameState::kPlaying);
}

bool GameStateManager::pause() {
    return transition_to(GameState::kPaused);
}

bool GameStateManager::resume() {
    return transition_to(GameState::kPlaying);
}

bool GameStateManager::begin_upgrade() {
    return transition_to(GameState::kUpgrade);
}

bool GameStateManager::complete_upgrade() {
    return transition_to(GameState::kPlaying);
}

bool GameStateManager::win() {
    return transition_to(GameState::kWon);
}

bool GameStateManager::lose() {
    return transition_to(GameState::kLost);
}

bool GameStateManager::restart() {
    if (!is_terminal()) {
        return false;
    }
    state_ = GameState::kMenu;
    return true;
}

} // namespace ai_survivors
