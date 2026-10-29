#include <gtest/gtest.h>

#include "core/game_timer.h"

namespace {

using ai_survivors::GameState;
using ai_survivors::GameStateManager;
using ai_survivors::GameTimer;
using AdvanceResult = ai_survivors::GameTimer::AdvanceResult;

struct SimulationOutcome {
    int advanced_ticks = 0;
    int win_ticks = 0;
    int frozen_ticks = 0;
    int rejected_ticks = 0;
    int unexpected_state_changes = 0;
    double elapsed_seconds = 0.0;
    GameState final_state = GameState::kMenu;
    bool win_reached = false;
};

// Accelerated deterministic simulation: 240000 fixed steps of 5 ms equal
// exactly 20 minutes of logical survival time. Pauses and upgrade selections
// are injected periodically and must not consume any of that time.
SimulationOutcome RunTwentyMinuteSimulation() {
    SimulationOutcome outcome;

    GameStateManager states;
    GameTimer timer(states);
    if (!states.start_run()) {
        ++outcome.unexpected_state_changes;
        return outcome;
    }

    constexpr int kTotalTicks = 240000;
    constexpr double kFixedStepSeconds = 0.005;

    for (int tick = 0; tick < kTotalTicks; ++tick) {
        if (tick % 6000 == 0) {
            if (!states.pause()) {
                ++outcome.unexpected_state_changes;
            }
            if (timer.advance(120.0) != AdvanceResult::kFrozen) {
                ++outcome.unexpected_state_changes;
            }
            if (!states.resume()) {
                ++outcome.unexpected_state_changes;
            }
        }

        if (tick % 7000 == 0) {
            if (!states.begin_upgrade()) {
                ++outcome.unexpected_state_changes;
            }
            if (timer.advance(300.0) != AdvanceResult::kFrozen) {
                ++outcome.unexpected_state_changes;
            }
            if (!states.complete_upgrade()) {
                ++outcome.unexpected_state_changes;
            }
        }

        switch (timer.advance(kFixedStepSeconds)) {
            case AdvanceResult::kAdvanced:
                ++outcome.advanced_ticks;
                break;
            case AdvanceResult::kWinReached:
                ++outcome.win_ticks;
                break;
            case AdvanceResult::kFrozen:
                ++outcome.frozen_ticks;
                break;
            case AdvanceResult::kRejected:
                ++outcome.rejected_ticks;
                break;
        }
    }

    outcome.elapsed_seconds = timer.elapsed_seconds();
    outcome.final_state = states.state();
    outcome.win_reached = timer.win_reached();
    return outcome;
}

void ExpectDeterministicOutcome(const SimulationOutcome &outcome) {
    EXPECT_EQ(outcome.advanced_ticks, 239999);
    EXPECT_EQ(outcome.win_ticks, 1);
    EXPECT_EQ(outcome.frozen_ticks, 0);
    EXPECT_EQ(outcome.rejected_ticks, 0);
    EXPECT_EQ(outcome.unexpected_state_changes, 0);
    EXPECT_DOUBLE_EQ(outcome.elapsed_seconds, 1200.0);
    EXPECT_EQ(outcome.final_state, GameState::kWon);
    EXPECT_TRUE(outcome.win_reached);
}

TEST(TwentyMinuteSimulationTest, AcceleratedFixedStepsReachExactBoundary) {
    const SimulationOutcome outcome = RunTwentyMinuteSimulation();
    ExpectDeterministicOutcome(outcome);
}

TEST(TwentyMinuteSimulationTest, RepeatedRunsAreDeterministic) {
    const SimulationOutcome first = RunTwentyMinuteSimulation();
    const SimulationOutcome second = RunTwentyMinuteSimulation();

    ExpectDeterministicOutcome(first);
    ExpectDeterministicOutcome(second);

    EXPECT_EQ(first.advanced_ticks, second.advanced_ticks);
    EXPECT_EQ(first.win_ticks, second.win_ticks);
    EXPECT_EQ(first.frozen_ticks, second.frozen_ticks);
    EXPECT_EQ(first.rejected_ticks, second.rejected_ticks);
    EXPECT_EQ(first.unexpected_state_changes,
              second.unexpected_state_changes);
    EXPECT_DOUBLE_EQ(first.elapsed_seconds, second.elapsed_seconds);
    EXPECT_EQ(first.final_state, second.final_state);
    EXPECT_EQ(first.win_reached, second.win_reached);
}

} // namespace
