#include <gtest/gtest.h>

#include "core/game_state.h"

namespace {

using ai_survivors::GameState;
using ai_survivors::GameStateManager;

TEST(GameStateManagerTest, StartsInMenu) {
    GameStateManager manager;

    EXPECT_EQ(manager.state(), GameState::kMenu);
    EXPECT_FALSE(manager.is_terminal());
    EXPECT_FALSE(manager.is_time_running());
}

TEST(GameStateManagerTest, ValidTransitionsFollowTable) {
    EXPECT_TRUE(GameStateManager::is_valid_transition(
        GameState::kMenu, GameState::kPlaying));

    EXPECT_TRUE(GameStateManager::is_valid_transition(
        GameState::kPlaying, GameState::kPaused));
    EXPECT_TRUE(GameStateManager::is_valid_transition(
        GameState::kPlaying, GameState::kUpgrade));
    EXPECT_TRUE(GameStateManager::is_valid_transition(
        GameState::kPlaying, GameState::kWon));
    EXPECT_TRUE(GameStateManager::is_valid_transition(
        GameState::kPlaying, GameState::kLost));

    EXPECT_TRUE(GameStateManager::is_valid_transition(
        GameState::kPaused, GameState::kPlaying));
    EXPECT_TRUE(GameStateManager::is_valid_transition(
        GameState::kUpgrade, GameState::kPlaying));

    // Terminal states have no ordinary transitions; restart() is the only
    // explicit exit and is intentionally absent from the transition table.
    for (GameState target : {GameState::kMenu, GameState::kPlaying,
                             GameState::kPaused, GameState::kUpgrade,
                             GameState::kWon, GameState::kLost}) {
        EXPECT_FALSE(GameStateManager::is_valid_transition(GameState::kWon,
                                                          target));
        EXPECT_FALSE(GameStateManager::is_valid_transition(GameState::kLost,
                                                          target));
    }
}

TEST(GameStateManagerTest, RunLifecycleTransitions) {
    GameStateManager manager;

    ASSERT_TRUE(manager.start_run());
    EXPECT_EQ(manager.state(), GameState::kPlaying);
    EXPECT_TRUE(manager.is_time_running());

    ASSERT_TRUE(manager.pause());
    EXPECT_EQ(manager.state(), GameState::kPaused);
    EXPECT_FALSE(manager.is_time_running());

    ASSERT_TRUE(manager.resume());
    EXPECT_EQ(manager.state(), GameState::kPlaying);
    EXPECT_TRUE(manager.is_time_running());

    ASSERT_TRUE(manager.begin_upgrade());
    EXPECT_EQ(manager.state(), GameState::kUpgrade);
    EXPECT_FALSE(manager.is_time_running());

    ASSERT_TRUE(manager.complete_upgrade());
    EXPECT_EQ(manager.state(), GameState::kPlaying);
    EXPECT_TRUE(manager.is_time_running());

    ASSERT_TRUE(manager.win());
    EXPECT_EQ(manager.state(), GameState::kWon);
    EXPECT_TRUE(manager.is_terminal());
    EXPECT_FALSE(manager.is_time_running());
}

TEST(GameStateManagerTest, LossIsTerminal) {
    GameStateManager manager;

    ASSERT_TRUE(manager.start_run());
    ASSERT_TRUE(manager.lose());

    EXPECT_EQ(manager.state(), GameState::kLost);
    EXPECT_TRUE(manager.is_terminal());
    EXPECT_FALSE(manager.is_time_running());
}

TEST(GameStateManagerTest, InvalidTransitionsAreRejectedWithoutMutation) {
    GameStateManager manager;

    // Menu only accepts Playing.
    EXPECT_FALSE(manager.pause());
    EXPECT_FALSE(manager.resume());
    EXPECT_FALSE(manager.begin_upgrade());
    EXPECT_FALSE(manager.complete_upgrade());
    EXPECT_FALSE(manager.win());
    EXPECT_FALSE(manager.lose());
    EXPECT_EQ(manager.state(), GameState::kMenu);

    ASSERT_TRUE(manager.start_run());

    // Equivalent/no-op transitions and transitions that only apply to other
    // states must fail while Playing.
    EXPECT_FALSE(manager.start_run());
    EXPECT_FALSE(manager.resume());
    EXPECT_FALSE(manager.complete_upgrade());
    EXPECT_FALSE(manager.transition_to(GameState::kPlaying));
    EXPECT_EQ(manager.state(), GameState::kPlaying);

    ASSERT_TRUE(manager.pause());
    EXPECT_FALSE(manager.pause());
    EXPECT_FALSE(manager.begin_upgrade());
    EXPECT_FALSE(manager.win());
    EXPECT_EQ(manager.state(), GameState::kPaused);

    ASSERT_TRUE(manager.resume());
    ASSERT_TRUE(manager.begin_upgrade());
    EXPECT_FALSE(manager.pause());
    EXPECT_FALSE(manager.begin_upgrade());
    EXPECT_FALSE(manager.lose());
    EXPECT_EQ(manager.state(), GameState::kUpgrade);
}

TEST(GameStateManagerTest, TerminalStatesRejectAllTransitions) {
    for (GameState terminal : {GameState::kWon, GameState::kLost}) {
        GameStateManager manager;
        ASSERT_TRUE(manager.start_run());
        if (terminal == GameState::kWon) {
            ASSERT_TRUE(manager.win());
        } else {
            ASSERT_TRUE(manager.lose());
        }

        EXPECT_EQ(manager.state(), terminal);
        EXPECT_FALSE(manager.start_run());
        EXPECT_FALSE(manager.pause());
        EXPECT_FALSE(manager.resume());
        EXPECT_FALSE(manager.begin_upgrade());
        EXPECT_FALSE(manager.complete_upgrade());
        EXPECT_FALSE(manager.win());
        EXPECT_FALSE(manager.lose());
        EXPECT_FALSE(manager.transition_to(GameState::kMenu));
        EXPECT_FALSE(manager.transition_to(GameState::kPlaying));
        EXPECT_FALSE(manager.transition_to(GameState::kPaused));
        EXPECT_FALSE(manager.transition_to(GameState::kUpgrade));
        EXPECT_EQ(manager.state(), terminal);
    }
}

TEST(GameStateManagerTest, RestartIsOnlyWayOutOfTerminal) {
    GameStateManager manager;
    ASSERT_TRUE(manager.start_run());
    ASSERT_TRUE(manager.win());

    ASSERT_TRUE(manager.restart());
    EXPECT_EQ(manager.state(), GameState::kMenu);
    EXPECT_FALSE(manager.is_terminal());

    ASSERT_TRUE(manager.start_run());
    ASSERT_TRUE(manager.lose());
    ASSERT_TRUE(manager.restart());
    EXPECT_EQ(manager.state(), GameState::kMenu);
}

TEST(GameStateManagerTest, RestartRejectedOutsideTerminal) {
    GameStateManager manager;
    EXPECT_FALSE(manager.restart());
    EXPECT_EQ(manager.state(), GameState::kMenu);

    ASSERT_TRUE(manager.start_run());
    EXPECT_FALSE(manager.restart());
    EXPECT_EQ(manager.state(), GameState::kPlaying);

    ASSERT_TRUE(manager.pause());
    EXPECT_FALSE(manager.restart());
    EXPECT_EQ(manager.state(), GameState::kPaused);

    ASSERT_TRUE(manager.resume());
    ASSERT_TRUE(manager.begin_upgrade());
    EXPECT_FALSE(manager.restart());
    EXPECT_EQ(manager.state(), GameState::kUpgrade);
}

TEST(GameStateManagerTest, CommandsCannotSubstituteForEachOther) {
    GameStateManager manager;

    // start_run is only valid from Menu.
    ASSERT_TRUE(manager.start_run());
    EXPECT_EQ(manager.state(), GameState::kPlaying);

    // Paused: start_run and complete_upgrade must be rejected without mutation.
    ASSERT_TRUE(manager.pause());
    EXPECT_FALSE(manager.start_run());
    EXPECT_FALSE(manager.complete_upgrade());
    EXPECT_EQ(manager.state(), GameState::kPaused);

    // resume is the only valid command from Paused.
    ASSERT_TRUE(manager.resume());
    EXPECT_EQ(manager.state(), GameState::kPlaying);

    // Upgrade: start_run and resume must be rejected without mutation.
    ASSERT_TRUE(manager.begin_upgrade());
    EXPECT_FALSE(manager.start_run());
    EXPECT_FALSE(manager.resume());
    EXPECT_EQ(manager.state(), GameState::kUpgrade);

    // complete_upgrade is the only valid command from Upgrade.
    ASSERT_TRUE(manager.complete_upgrade());
    EXPECT_EQ(manager.state(), GameState::kPlaying);
}

} // namespace
