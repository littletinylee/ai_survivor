#include <gtest/gtest.h>

#include <limits>

#include "core/game_timer.h"

namespace {

using ai_survivors::GameState;
using ai_survivors::GameStateManager;
using ai_survivors::GameTimer;
using AdvanceResult = ai_survivors::GameTimer::AdvanceResult;

TEST(GameTimerTest, StartsAtZero) {
    GameStateManager states;
    GameTimer timer(states);

    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 0.0);
    EXPECT_DOUBLE_EQ(timer.remaining_seconds(), 1200.0);
    EXPECT_FALSE(timer.win_reached());
}

TEST(GameTimerTest, AdvancesOnlyWhilePlaying) {
    GameStateManager states;
    GameTimer timer(states);

    // Menu freezes the clock.
    EXPECT_EQ(timer.advance(10.0), AdvanceResult::kFrozen);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 0.0);

    ASSERT_TRUE(states.start_run());
    EXPECT_EQ(timer.advance(10.0), AdvanceResult::kAdvanced);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 10.0);
    EXPECT_DOUBLE_EQ(timer.remaining_seconds(), 1190.0);

    // Paused freezes the clock.
    ASSERT_TRUE(states.pause());
    EXPECT_EQ(timer.advance(5.0), AdvanceResult::kFrozen);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 10.0);

    // Upgrade selection freezes the clock.
    ASSERT_TRUE(states.resume());
    ASSERT_TRUE(states.begin_upgrade());
    EXPECT_EQ(timer.advance(50.0), AdvanceResult::kFrozen);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 10.0);

    ASSERT_TRUE(states.complete_upgrade());
    EXPECT_EQ(timer.advance(2.5), AdvanceResult::kAdvanced);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 12.5);
}

TEST(GameTimerTest, PauseAndUpgradeDoNotConsumeSurvivalTime) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());

    ASSERT_EQ(timer.advance(100.0), AdvanceResult::kAdvanced);
    ASSERT_TRUE(states.pause());
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(timer.advance(60.0), AdvanceResult::kFrozen);
    }
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 100.0);
    ASSERT_TRUE(states.resume());

    ASSERT_TRUE(states.begin_upgrade());
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(timer.advance(60.0), AdvanceResult::kFrozen);
    }
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 100.0);
    ASSERT_TRUE(states.complete_upgrade());

    ASSERT_EQ(timer.advance(100.0), AdvanceResult::kAdvanced);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 200.0);
}

TEST(GameTimerTest, RejectsNegativeAndNonFiniteDeltas) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());
    ASSERT_EQ(timer.advance(10.0), AdvanceResult::kAdvanced);

    const double invalid[] = {
        -0.001,
        -1.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };

    for (double delta : invalid) {
        EXPECT_EQ(timer.advance(delta), AdvanceResult::kRejected);
        EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 10.0);
        EXPECT_EQ(states.state(), GameState::kPlaying);
    }

    // Negative zero is a finite non-negative delta.
    EXPECT_EQ(timer.advance(-0.0), AdvanceResult::kAdvanced);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 10.0);
}

TEST(GameTimerTest, RejectsInvalidDeltasEvenWhenFrozenOrTerminal) {
    GameStateManager states;
    GameTimer timer(states);

    // Frozen in Menu.
    EXPECT_EQ(timer.advance(-1.0), AdvanceResult::kRejected);
    EXPECT_EQ(timer.advance(std::numeric_limits<double>::quiet_NaN()),
              AdvanceResult::kRejected);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 0.0);

    ASSERT_TRUE(states.start_run());
    ASSERT_EQ(timer.advance(1200.0), AdvanceResult::kWinReached);
    EXPECT_EQ(states.state(), GameState::kWon);

    // Terminal, but malformed input is still explicitly rejected.
    EXPECT_EQ(timer.advance(-1.0), AdvanceResult::kRejected);
    EXPECT_EQ(timer.advance(std::numeric_limits<double>::infinity()),
              AdvanceResult::kRejected);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 1200.0);
}

TEST(GameTimerTest, ExactTwentyMinuteBoundary) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());

    EXPECT_EQ(timer.advance(1199.999), AdvanceResult::kAdvanced);
    EXPECT_FALSE(timer.win_reached());
    EXPECT_EQ(states.state(), GameState::kPlaying);

    EXPECT_EQ(timer.advance(0.001), AdvanceResult::kWinReached);
    EXPECT_TRUE(timer.win_reached());
    EXPECT_EQ(states.state(), GameState::kWon);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 1200.0);
    EXPECT_DOUBLE_EQ(timer.remaining_seconds(), 0.0);
}

TEST(GameTimerTest, SingleStepOfFullDurationWins) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());

    EXPECT_EQ(timer.advance(GameTimer::kWinDurationSeconds),
              AdvanceResult::kWinReached);
    EXPECT_EQ(states.state(), GameState::kWon);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 1200.0);
}

TEST(GameTimerTest, OversizedDeltaClampsToExactBoundary) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());

    EXPECT_EQ(timer.advance(1e12), AdvanceResult::kWinReached);
    EXPECT_EQ(states.state(), GameState::kWon);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 1200.0);
}

TEST(GameTimerTest, TerminalClockIsFrozenUntilExplicitRestart) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());
    ASSERT_EQ(timer.advance(1200.0), AdvanceResult::kWinReached);

    // Terminal state is immutable and the clock stays at the boundary.
    EXPECT_EQ(timer.advance(1.0), AdvanceResult::kFrozen);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 1200.0);
    EXPECT_FALSE(states.pause());
    EXPECT_FALSE(states.lose());
    EXPECT_EQ(states.state(), GameState::kWon);

    ASSERT_TRUE(timer.restart());
    EXPECT_EQ(states.state(), GameState::kMenu);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 0.0);
    EXPECT_FALSE(timer.win_reached());

    ASSERT_TRUE(states.start_run());
    EXPECT_EQ(timer.advance(1200.0), AdvanceResult::kWinReached);
    EXPECT_EQ(states.state(), GameState::kWon);
}

TEST(GameTimerTest, RestartRejectedOutsideTerminal) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());
    ASSERT_EQ(timer.advance(25.0), AdvanceResult::kAdvanced);

    EXPECT_FALSE(timer.restart());
    EXPECT_EQ(states.state(), GameState::kPlaying);
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 25.0);
}

} // namespace
