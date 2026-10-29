#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "core/character.h"
#include "core/player_controller.h"

namespace ai_survivors {
namespace {

CharacterDefinition make(const std::string &id) {
    CharacterDefinition c;
    c.id = id;
    c.name = "Name";
    c.description = "Desc";
    c.initial_weapon_id = "bubble_shot";
    c.passive_id = "passive_one";
    c.passive_description = "Does something";
    return c;
}

TEST(CharacterRegistry, AcceptsValidAndFinds) {
    CharacterRegistry r;
    EXPECT_EQ(r.add(make("fish")), "");
    ASSERT_NE(r.find("fish"), nullptr);
    EXPECT_EQ(r.find("fish")->move_speed, 220.0);
    EXPECT_EQ(r.find("other"), nullptr);
}

TEST(CharacterRegistry, RejectsDuplicateIds) {
    CharacterRegistry r;
    EXPECT_EQ(r.add(make("fish")), "");
    EXPECT_NE(r.add(make("fish")), "");
    EXPECT_EQ(r.size(), 1u);
}

TEST(CharacterRegistry, AcceptsSecondConfigurationWithoutCodeChange) {
    CharacterRegistry r;
    EXPECT_EQ(r.add(make("fish")), "");
    CharacterDefinition second = make("crab");
    second.move_speed = 150.0;
    second.max_hp = 180.0;
    EXPECT_EQ(r.add(second), "");
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r.find("crab")->move_speed, 150.0);
}

TEST(CharacterRegistry, RejectsInvalidIds) {
    CharacterRegistry r;
    for (const char *bad : {"", "Fish", "1fish", "a b", "a-b"}) {
        EXPECT_NE(r.add(make(bad)), "") << bad;
    }
    EXPECT_NE(r.add(make(std::string(65, 'a'))), "");
    EXPECT_TRUE(r.empty());
}

TEST(CharacterRegistry, RejectsInvalidStats) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CharacterRegistry r;
    const auto rejects = [&](auto mutate) {
        CharacterDefinition c = make("x");
        mutate(c);
        return !validate_character(c).empty();
    };
    EXPECT_TRUE(rejects([&](auto &c) { c.max_hp = 0; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.max_hp = nan; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.move_speed = -1; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.move_speed = inf; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.armor = -0.1; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.damage_multiplier = 0; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.attack_speed_multiplier = nan; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.crit_chance = 1.5; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.crit_chance = -0.1; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.xp_multiplier = 0; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.pickup_radius = -5; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.initial_weapon_id = ""; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.passive_id = "Bad Id"; }));
    EXPECT_TRUE(rejects([&](auto &c) { c.name = ""; }));
    EXPECT_FALSE(rejects([&](auto &c) { c.portrait_asset = ""; }));
}

TEST(PlayerController, MovesAtCharacterSpeed) {
    PlayerController p(500, 300, 20, 100);
    EXPECT_TRUE(p.step(GameState::kPlaying, 1.0, 1, 0));
    EXPECT_DOUBLE_EQ(p.position().x, 100.0);
    EXPECT_TRUE(p.set_move_speed(220.0));
    p.reset();
    p.step(GameState::kPlaying, 0.5, 0, -1);
    EXPECT_DOUBLE_EQ(p.position().y, -110.0);
    EXPECT_FALSE(p.set_move_speed(0.0));
    EXPECT_EQ(p.move_speed(), 220.0);
}

TEST(PlayerController, DiagonalIsNotFaster) {
    PlayerController p(5000, 5000, 0, 200);
    p.step(GameState::kPlaying, 1.0, 1, 1);
    const double dist = std::hypot(p.position().x, p.position().y);
    EXPECT_NEAR(dist, 200.0, 1e-9);
    PlayerController q(5000, 5000, 0, 200);
    q.step(GameState::kPlaying, 1.0, 50, 50);  // magnitude of input is ignored
    EXPECT_NEAR(std::hypot(q.position().x, q.position().y), 200.0, 1e-9);
}

TEST(PlayerController, ClampsToArenaBounds) {
    PlayerController p(500, 300, 20, 1000);
    p.step(GameState::kPlaying, 10.0, 1, 1);
    EXPECT_DOUBLE_EQ(p.position().x, 480.0);
    EXPECT_DOUBLE_EQ(p.position().y, 280.0);
    p.step(GameState::kPlaying, 10.0, -1, -1);
    EXPECT_DOUBLE_EQ(p.position().x, -480.0);
    EXPECT_DOUBLE_EQ(p.position().y, -280.0);
}

TEST(PlayerController, FrozenOutsidePlaying) {
    PlayerController p(500, 300, 20, 100);
    for (GameState s : {GameState::kMenu, GameState::kPaused,
                        GameState::kUpgrade, GameState::kWon,
                        GameState::kLost}) {
        EXPECT_FALSE(p.step(s, 1.0, 1, 0));
        EXPECT_EQ(p.position().x, 0.0);
    }
}

TEST(PlayerController, RejectsNonFiniteInput) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    PlayerController p(500, 300, 20, 100);
    EXPECT_FALSE(p.step(GameState::kPlaying, nan, 1, 0));
    EXPECT_FALSE(p.step(GameState::kPlaying, 1.0, nan, 0));
    EXPECT_FALSE(p.step(GameState::kPlaying, -1.0, 1, 0));
    EXPECT_EQ(p.position().x, 0.0);
    EXPECT_TRUE(p.step(GameState::kPlaying, 1.0, 0, 0));  // no input, no move
    EXPECT_EQ(p.position().x, 0.0);
}

} // namespace
} // namespace ai_survivors
