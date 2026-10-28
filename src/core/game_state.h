#pragma once

namespace ai_survivors {

// Menu is the only non-terminal "idle" state. A run starts by moving from
// Menu to Playing. Paused and Upgrade both freeze the survival clock.
enum class GameState {
    kMenu,
    kPlaying,
    kPaused,
    kUpgrade,
    kWon,
    kLost,
};

const char *to_string(GameState state);

// Pure C++ state machine. It has no Godot dependency.
//
// Transition table:
//   Menu     -> Playing
//   Playing  -> Paused | Upgrade | Won | Lost
//   Paused   -> Playing
//   Upgrade  -> Playing
//   Won/Lost -> (terminal; only restart() may leave them)
//
// restart() is deliberately separate from transition_to(): terminal states can
// only be left through the explicit restart operation.
class GameStateManager {
public:
    GameStateManager() = default;

    GameState state() const { return state_; }

    bool is_terminal() const;
    bool is_time_running() const;

    static bool is_valid_transition(GameState from, GameState to);

    bool transition_to(GameState next);
    bool start_run();
    bool pause();
    bool resume();
    bool begin_upgrade();
    bool complete_upgrade();
    bool win();
    bool lose();
    bool restart();

private:
    GameState state_ = GameState::kMenu;
};

} // namespace ai_survivors
