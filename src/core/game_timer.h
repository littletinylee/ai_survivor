#pragma once

#include <cstdint>

#include "core/game_state.h"

namespace ai_survivors {

// Survival clock for one run.
//
// Time only advances while GameStateManager is in Playing. Paused, Upgrade,
// Menu, Won and Lost all freeze the clock. Deltas must be finite and
// non-negative; rejected deltas never change the clock. The clock is capped at
// exactly 1200 seconds and the state manager is moved to Won at that boundary.
//
// The clock is stored as integer nanoseconds so an accelerated simulation made
// of many small fixed steps reaches the 20 minute boundary deterministically.
class GameTimer {
public:
    enum class AdvanceResult {
        kRejected,   // delta was negative, NaN or infinite; nothing changed
        kFrozen,     // valid delta, but time is not running or the run is over
        kAdvanced,   // valid delta was consumed while Playing
        kWinReached, // this delta reached the 1200 second boundary; state is Won
    };

    static constexpr double kWinDurationSeconds = 1200.0;
    static constexpr std::int64_t kWinDurationNanoseconds =
        1'200'000'000'000LL;

    explicit GameTimer(GameStateManager &states);

    GameTimer(const GameTimer &) = delete;
    GameTimer &operator=(const GameTimer &) = delete;

    AdvanceResult advance(double delta_seconds);

    double elapsed_seconds() const;
    double remaining_seconds() const;
    bool win_reached() const;

    // Explicit restart path: terminal -> Menu and clock -> zero. Returns false
    // when the run is not terminal.
    bool restart();

    // Reset only the clock. The caller owns the state transition.
    void reset();

private:
    AdvanceResult reach_win_boundary();

    GameStateManager &states_;
    std::int64_t elapsed_nanoseconds_ = 0;
};

} // namespace ai_survivors
