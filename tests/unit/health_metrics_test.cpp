#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

#include <gtest/gtest.h>

#include "core/combat.h"

using namespace ai_survivors;

namespace {

constexpr double kStep = 1.0 / 60.0;

CombatConfig manual_config() {
    CombatConfig c;
    c.waves_enabled = false;
    return c;
}

// Stationary enemy on the player's start position; hp is effectively infinite.
EnemySpawn stationary(EnemyKind kind, double contact_damage) {
    EnemySpawn s;
    s.position = Vec2{0.0, 0.0};
    s.hp = 1e6;
    s.speed = 0.0;
    s.contact_damage = contact_damage;
    s.kind = kind;
    return s;
}

// Well outside contact range of the enemy at the origin.
Vec2 away() { return Vec2{1000.0, 1000.0}; }

std::size_t kind_index(EnemyKind kind) {
    return static_cast<std::size_t>(kind);
}

} // namespace

TEST(HealthMetrics, StartsAtZero) {
    CombatWorld w(CombatStats(), 1, manual_config());
    const HealthMetrics m = w.health_metrics();
    EXPECT_DOUBLE_EQ(m.damage_taken, 0.0);
    EXPECT_DOUBLE_EQ(m.healing_received, 0.0);
    EXPECT_DOUBLE_EQ(m.max_hp_gain, 0.0);
    EXPECT_EQ(m.contact_hits, 0u);
    for (std::size_t i = 0; i < HealthMetrics::kKindCount; ++i) {
        EXPECT_EQ(m.contact_hits_by_kind[i], 0u);
        EXPECT_DOUBLE_EQ(m.contact_damage_by_kind[i], 0.0);
    }
}

TEST(HealthMetrics, ArmoredHitCountsMitigatedNotRawDamage) {
    GameStateManager st;
    st.start_run();
    CombatStats s;
    s.armor = 100.0; // 10 raw -> 5 after armor
    CombatWorld w(s, 1, manual_config());
    w.spawn_enemy(stationary(EnemyKind::kChaser, 10.0));
    w.tick(st, kStep, Vec2{});
    const HealthMetrics m = w.health_metrics();
    EXPECT_DOUBLE_EQ(w.hp(), 95.0);
    EXPECT_DOUBLE_EQ(m.damage_taken, 5.0);
    EXPECT_EQ(m.contact_hits, 1u);
    EXPECT_DOUBLE_EQ(m.contact_damage_by_kind[kind_index(EnemyKind::kChaser)], 5.0);
}

TEST(HealthMetrics, OverkillDamageIsCappedToRemainingHp) {
    GameStateManager st;
    st.start_run();
    CombatStats s;
    s.max_hp = 5.0;
    CombatWorld w(s, 1, manual_config());
    w.spawn_enemy(stationary(EnemyKind::kChaser, 10.0));
    EXPECT_EQ(w.tick(st, kStep, Vec2{}), CombatWorld::TickResult::kPlayerDied);
    const HealthMetrics m = w.health_metrics();
    EXPECT_DOUBLE_EQ(w.hp(), 0.0);
    EXPECT_DOUBLE_EQ(m.damage_taken, 5.0); // not the raw 10
    EXPECT_EQ(m.contact_hits, 1u);
    EXPECT_DOUBLE_EQ(m.contact_damage_by_kind[kind_index(EnemyKind::kChaser)], 5.0);
}

TEST(HealthMetrics, ZeroDamageContactIsNotCountedAsHit) {
    GameStateManager st;
    st.start_run();
    CombatWorld w(CombatStats(), 1, manual_config());
    w.spawn_enemy(stationary(EnemyKind::kChaser, 0.0));
    w.tick(st, kStep, Vec2{});
    const HealthMetrics m = w.health_metrics();
    EXPECT_DOUBLE_EQ(w.hp(), 100.0);
    EXPECT_EQ(m.contact_hits, 0u);
    EXPECT_DOUBLE_EQ(m.damage_taken, 0.0);
}

TEST(HealthMetrics, IframesBlockButDoNotCountAsHits) {
    GameStateManager st;
    st.start_run();
    CombatWorld w(CombatStats(), 1, manual_config());
    w.spawn_enemy(stationary(EnemyKind::kChaser, 10.0));
    w.spawn_enemy(stationary(EnemyKind::kBrute, 16.0));
    // Both overlap the player in the same frame; the first hit grants iframes,
    // so the brute is skipped and must not appear in the metrics.
    w.tick(st, kStep, Vec2{});
    HealthMetrics m = w.health_metrics();
    EXPECT_EQ(m.contact_hits, 1u);
    EXPECT_DOUBLE_EQ(m.damage_taken, 10.0);
    EXPECT_EQ(m.contact_hits_by_kind[kind_index(EnemyKind::kBrute)], 0u);

    // Half a second later iframes have expired, the brute lands its hit, and
    // the chaser is still on its 1 s contact cooldown.
    for (int i = 1; i < 30; ++i) w.tick(st, kStep, Vec2{});
    m = w.health_metrics();
    EXPECT_EQ(m.contact_hits, 2u);
    EXPECT_DOUBLE_EQ(m.damage_taken, 26.0);
    EXPECT_EQ(m.contact_hits_by_kind[kind_index(EnemyKind::kChaser)], 1u);
    EXPECT_EQ(m.contact_hits_by_kind[kind_index(EnemyKind::kBrute)], 1u);
    EXPECT_DOUBLE_EQ(w.hp(), 74.0);
}

TEST(HealthMetrics, ContactHitsSplitByEnemyKind) {
    auto run = [](EnemyKind kind, double contact) {
        GameStateManager st;
        st.start_run();
        CombatWorld w(CombatStats(), 1, manual_config());
        w.spawn_enemy(stationary(kind, contact));
        w.tick(st, kStep, Vec2{});
        return w.health_metrics();
    };
    const struct {
        EnemyKind kind;
        double contact;
    } cases[] = {
        {EnemyKind::kChaser, 10.0},
        {EnemyKind::kCharger, 13.0},
        {EnemyKind::kBrute, 16.0},
    };
    for (const auto &c : cases) {
        const HealthMetrics m = run(c.kind, c.contact);
        EXPECT_EQ(m.contact_hits, 1u);
        EXPECT_DOUBLE_EQ(m.damage_taken, c.contact);
        for (std::size_t i = 0; i < HealthMetrics::kKindCount; ++i) {
            const bool own = i == kind_index(c.kind);
            EXPECT_EQ(m.contact_hits_by_kind[i], own ? 1u : 0u);
            EXPECT_DOUBLE_EQ(m.contact_damage_by_kind[i], own ? c.contact : 0.0);
        }
    }
}

TEST(HealthMetrics, HealingCountsOnlyActualRestoredHp) {
    GameStateManager st;
    st.start_run();
    CombatWorld w(CombatStats(), 1, manual_config());
    w.spawn_enemy(stationary(EnemyKind::kChaser, 10.0));
    w.tick(st, kStep, Vec2{});  // one 10 HP contact hit
    w.tick(st, kStep, away());  // step clear so no further hits
    ASSERT_DOUBLE_EQ(w.hp(), 90.0);

    // Rejected calls restore nothing and count nothing.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_DOUBLE_EQ(w.heal(GameState::kPlaying, nan), 0.0);
    EXPECT_DOUBLE_EQ(w.heal(GameState::kPlaying, inf), 0.0);
    EXPECT_DOUBLE_EQ(w.heal(GameState::kPlaying, -5.0), 0.0);
    EXPECT_DOUBLE_EQ(w.heal(GameState::kPlaying, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(w.heal(GameState::kPaused, 5.0), 0.0);
    EXPECT_DOUBLE_EQ(w.heal(GameState::kLost, 5.0), 0.0);
    EXPECT_DOUBLE_EQ(w.health_metrics().healing_received, 0.0);

    // Over-heal is capped at max HP: only the 10 missing HP is counted.
    EXPECT_DOUBLE_EQ(w.heal(GameState::kPlaying, 30.0), 10.0);
    EXPECT_DOUBLE_EQ(w.health_metrics().healing_received, 10.0);

    // fullHP heal restores nothing and counts nothing.
    EXPECT_DOUBLE_EQ(w.heal(GameState::kPlaying, 5.0), 0.0);
    EXPECT_DOUBLE_EQ(w.health_metrics().healing_received, 10.0);
    EXPECT_DOUBLE_EQ(w.health_metrics().damage_taken, 10.0);
}

TEST(HealthMetrics, MaxHpUpgradeCountsOnlyActualGain) {
    CombatWorld w(CombatStats(), 1, manual_config()); // max 100

    CombatStats up;
    up.max_hp = 120.0;
    ASSERT_TRUE(w.update_player_stats(up, 1.0));
    EXPECT_DOUBLE_EQ(w.hp(), 120.0);
    EXPECT_DOUBLE_EQ(w.health_metrics().max_hp_gain, 20.0);

    // A max HP decrease clamps HP but is not a gain.
    CombatStats down;
    down.max_hp = 80.0;
    ASSERT_TRUE(w.update_player_stats(down, 1.0));
    EXPECT_DOUBLE_EQ(w.hp(), 80.0);
    EXPECT_DOUBLE_EQ(w.health_metrics().max_hp_gain, 20.0);

    CombatStats back;
    back.max_hp = 100.0;
    ASSERT_TRUE(w.update_player_stats(back, 1.0));
    EXPECT_DOUBLE_EQ(w.hp(), 100.0);
    EXPECT_DOUBLE_EQ(w.health_metrics().max_hp_gain, 40.0);
    // Upgrades are not healing.
    EXPECT_DOUBLE_EQ(w.health_metrics().healing_received, 0.0);
}

TEST(HealthMetrics, RejectedStatChangesCountNothing) {
    CombatWorld w(CombatStats(), 1, manual_config());
    const double nan = std::numeric_limits<double>::quiet_NaN();

    CombatStats bad_hp;
    bad_hp.max_hp = nan;
    EXPECT_FALSE(w.update_player_stats(bad_hp, 1.0));

    CombatStats big;
    big.max_hp = 500.0;
    EXPECT_FALSE(w.update_player_stats(big, 0.0)); // scale must be positive
    EXPECT_FALSE(w.update_player_stats(big, nan));

    const HealthMetrics m = w.health_metrics();
    EXPECT_DOUBLE_EQ(m.max_hp_gain, 0.0);
    EXPECT_DOUBLE_EQ(w.max_hp(), 100.0);
    EXPECT_DOUBLE_EQ(w.hp(), 100.0);
}

TEST(HealthMetrics, FrozenTicksCountNothing) {
    GameStateManager st;
    st.start_run();
    ASSERT_TRUE(st.pause());
    CombatWorld w(CombatStats(), 1, manual_config());
    w.spawn_enemy(stationary(EnemyKind::kChaser, 10.0));
    EXPECT_EQ(w.tick(st, kStep, Vec2{}), CombatWorld::TickResult::kFrozen);
    const HealthMetrics m = w.health_metrics();
    EXPECT_DOUBLE_EQ(w.hp(), 100.0);
    EXPECT_EQ(m.contact_hits, 0u);
    EXPECT_DOUBLE_EQ(m.damage_taken, 0.0);
}

TEST(HealthMetrics, NewWorldStartsFresh) {
    GameStateManager st;
    st.start_run();
    CombatWorld a(CombatStats(), 1, manual_config());
    a.spawn_enemy(stationary(EnemyKind::kCharger, 13.0));
    a.tick(st, kStep, Vec2{});
    a.tick(st, kStep, away());
    a.heal(GameState::kPlaying, 1.0);
    CombatStats up;
    up.max_hp = 150.0;
    ASSERT_TRUE(a.update_player_stats(up, 1.0));
    ASSERT_GT(a.health_metrics().contact_hits, 0u);
    ASSERT_GT(a.health_metrics().max_hp_gain, 0.0);

    CombatWorld b(CombatStats(), 1, manual_config());
    const HealthMetrics m = b.health_metrics();
    EXPECT_DOUBLE_EQ(m.damage_taken, 0.0);
    EXPECT_DOUBLE_EQ(m.healing_received, 0.0);
    EXPECT_DOUBLE_EQ(m.max_hp_gain, 0.0);
    EXPECT_EQ(m.contact_hits, 0u);
    for (std::size_t i = 0; i < HealthMetrics::kKindCount; ++i) {
        EXPECT_EQ(m.contact_hits_by_kind[i], 0u);
        EXPECT_DOUBLE_EQ(m.contact_damage_by_kind[i], 0.0);
    }
}
