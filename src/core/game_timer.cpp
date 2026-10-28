#include "core/game_timer.h"

#include <cmath>

namespace ai_survivors {
namespace {
constexpr long double kNanosecondsPerSecond = 1'000'000'000.0L;
}

GameTimer::GameTimer(GameStateManager &states) : states_(states) {}

GameTimer::AdvanceResult GameTimer::reach_win_boundary() {
    if (!states_.win()) {
        // Defensive: time should only be running while Playing, so win() must
        // succeed. If some other code changed the state, never mutate the
        // clock or claim a win.
        return AdvanceResult::kFrozen;
    }
    elapsed_nanoseconds_ = kWinDurationNanoseconds;
    return AdvanceResult::kWinReached;
}

GameTimer::AdvanceResult GameTimer::advance(double delta_seconds) {
    // Validate first, even while frozen or terminal. Invalid input must never
    // be silently ignored behind a state check.
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) {
        return AdvanceResult::kRejected;
    }

    if (!states_.is_time_running()) {
        return AdvanceResult::kFrozen;
    }

    if (elapsed_nanoseconds_ >= kWinDurationNanoseconds) {
        return AdvanceResult::kFrozen;
    }

    const std::int64_t remaining_nanoseconds =
        kWinDurationNanoseconds - elapsed_nanoseconds_;
    const double remaining_seconds =
        static_cast<double>(remaining_nanoseconds) / 1e9;

    // Compare before converting to integer nanoseconds so very large finite
    // deltas cannot overflow the conversion, and the exact 20 minute boundary
    // is not subject to accumulated floating point error.
    if (delta_seconds >= remaining_seconds) {
        return reach_win_boundary();
    }

    const long double scaled =
        static_cast<long double>(delta_seconds) * kNanosecondsPerSecond;
    const std::int64_t delta_nanoseconds =
        static_cast<std::int64_t>(std::llround(scaled));

    if (delta_nanoseconds <= 0) {
        // A positive delta smaller than half a nanosecond is accepted but does
        // not measurably advance the clock.
        return AdvanceResult::kAdvanced;
    }

    const std::int64_t next_elapsed =
        elapsed_nanoseconds_ + delta_nanoseconds;
    if (next_elapsed >= kWinDurationNanoseconds) {
        return reach_win_boundary();
    }

    elapsed_nanoseconds_ = next_elapsed;
    return AdvanceResult::kAdvanced;
}

double GameTimer::elapsed_seconds() const {
    return static_cast<double>(elapsed_nanoseconds_) / 1e9;
}

double GameTimer::remaining_seconds() const {
    if (elapsed_nanoseconds_ >= kWinDurationNanoseconds) {
        return 0.0;
    }
    return static_cast<double>(kWinDurationNanoseconds - elapsed_nanoseconds_) /
           1e9;
}

bool GameTimer::win_reached() const {
    return elapsed_nanoseconds_ >= kWinDurationNanoseconds;
}

bool GameTimer::restart() {
    if (!states_.restart()) {
        return false;
    }
    elapsed_nanoseconds_ = 0;
    return true;
}

void GameTimer::reset() {
    elapsed_nanoseconds_ = 0;
}

} // namespace ai_survivors
