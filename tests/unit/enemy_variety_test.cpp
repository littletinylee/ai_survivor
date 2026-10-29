#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>

#include "core/combat.h"

using namespace ai_survivors;

namespace {

constexpr double kH = 1.0 / 60.0;

EnemySpawn charger_spec(Vec2 pos, double speed = 100.0) {
    EnemySpawn s;
    s.position = pos;
    s.speed = speed;
    s.kind = EnemyKind::kCharger;
    return s;
}

CombatConfig quiet_config() {
    CombatConfig c;
    c.waves_enabled = false;
    return c;
}

void step_for(EnemyManager &m, double seconds, Vec2 target) {
    const int n = static_cast<int>(std::lround(seconds / kH));
    for (int i = 0; i < n; ++i) m.step(kH, target, 520.0, 290.0);
}

} // namespace

TEST(EnemyVariety, DefaultSpawnIsChaserAndStepUnchanged) {
    EnemyManager m;
    EnemySpawn s;
    s.position = Vec2{100.0, 0.0};
    s.speed = 50.0;
    ASSERT_TRUE(m.spawn(s, 18.0));
    EXPECT_EQ(m.enemies()[0].kind, EnemyKind::kChaser);
    EXPECT_EQ(m.enemies()[0].radius, 18.0);
    m.step(1.0, Vec2{0.0, 0.0});
    EXPECT_NEAR(m.enemies()[0].position.x, 50.0, 1e-9);
}

TEST(EnemyVariety, InvalidKindOrRadiusRejected) {
    EnemyManager m;
    EnemySpawn s;
    s.kind = static_cast<EnemyKind>(99);
    EXPECT_FALSE(m.spawn(s, 18.0));
    s.kind = EnemyKind::kChaser;
    s.radius = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(m.spawn(s, 18.0));
    s.radius = -1.0;
    EXPECT_FALSE(m.spawn(s, 18.0));
    s.radius = 1000.0;
    EXPECT_FALSE(m.spawn(s, 18.0));
    EXPECT_TRUE(m.enemies().empty());
    s.radius = 0.0;
    EXPECT_TRUE(m.spawn(s, 18.0));
    EXPECT_EQ(m.enemies()[0].id, 1u); // rejected spawns consume no ids
}

TEST(EnemyVariety, ChargerWindupIsStationaryAndLocksAim) {
    EnemyManager m;
    ASSERT_TRUE(m.spawn(charger_spec(Vec2{-200.0, 0.0}), 14.0));
    m.step(kH, Vec2{0.0, 0.0}, 520.0, 290.0);
    const Enemy &e = m.enemies()[0];
    ASSERT_EQ(e.phase, ChargerPhase::kWindup);
    EXPECT_NEAR(e.aim.x, 1.0, 1e-9);
    const Vec2 start = e.position;
    step_for(m, 0.5, Vec2{0.0, 250.0}); // player moves away mid-windup
    EXPECT_EQ(e.phase, ChargerPhase::kWindup);
    EXPECT_EQ(e.position.x, start.x);
    EXPECT_EQ(e.position.y, start.y);
    EXPECT_NEAR(e.aim.x, 1.0, 1e-9);
    EXPECT_NEAR(e.aim.y, 0.0, 1e-9);
}

TEST(EnemyVariety, DashFollowsLockedVectorThenRecoversAndRearms) {
    EnemyManager m;
    ASSERT_TRUE(m.spawn(charger_spec(Vec2{-200.0, 0.0}), 14.0));
    const Vec2 player_far{0.0, 250.0};
    m.step(kH, Vec2{0.0, 0.0}, 520.0, 290.0);       // windup begins, aim = +x
    step_for(m, 0.7 - kH, player_far);               // first call already consumed kH
    const Enemy &e = m.enemies()[0];
    ASSERT_EQ(e.phase, ChargerPhase::kDash);
    const double y0 = e.position.y;
    const double x0 = e.position.x;
    step_for(m, 0.3, player_far);
    EXPECT_NEAR(e.position.y, y0, 1e-9);             // did not home to the player
    EXPECT_NEAR(e.position.x - x0, 100.0 * 3.0 * 0.3, 1e-6);
    ASSERT_EQ(e.phase, ChargerPhase::kRecovery);
    const Vec2 rec = e.position;
    step_for(m, 0.5, player_far);
    EXPECT_EQ(e.position.x, rec.x);
    EXPECT_EQ(e.position.y, rec.y);
    step_for(m, 0.3, Vec2{e.position.x + 50.0, e.position.y}); // exactly 0.8s recovery
    EXPECT_EQ(e.phase, ChargerPhase::kApproach);
    EXPECT_GT(e.rearm_remaining, 0.0);
    // Player is in range, but it cannot charge again for the 1 s rearm.
    const Vec2 near_player{e.position.x + 150.0, e.position.y};
    step_for(m, 0.9, near_player);
    EXPECT_EQ(e.phase, ChargerPhase::kApproach);
    step_for(m, 0.2, near_player);
    EXPECT_EQ(e.phase, ChargerPhase::kWindup);
}

TEST(EnemyVariety, ChargerFiniteAtZeroDistanceHugeDtAndArenaEdge) {
    EnemyManager m;
    ASSERT_TRUE(m.spawn(charger_spec(Vec2{0.0, 0.0}), 14.0));
    m.step(kH, Vec2{0.0, 0.0}, 520.0, 290.0);
    EXPECT_EQ(m.enemies()[0].aim.x, 1.0);
    const double before = m.enemies()[0].phase_remaining;
    m.step(1e300, Vec2{0.0, 0.0}, 520.0, 290.0);
    EXPECT_EQ(m.enemies()[0].phase, ChargerPhase::kWindup);
    EXPECT_NEAR(m.enemies()[0].phase_remaining,
                before - ChargerTuning::kMaxStepDelta, 1e-12);
    const double after = m.enemies()[0].phase_remaining;
    m.step(std::numeric_limits<double>::infinity(), Vec2{0.0, 0.0}, 520.0, 290.0);
    m.step(std::numeric_limits<double>::quiet_NaN(), Vec2{0.0, 0.0}, 520.0, 290.0);
    EXPECT_EQ(m.enemies()[0].phase_remaining, after);
    EnemyManager edge;
    ASSERT_TRUE(edge.spawn(charger_spec(Vec2{500.0, 0.0}, 1000.0), 14.0));
    edge.step(kH, Vec2{510.0, 0.0}, 520.0, 290.0);
    step_for(edge, 1.0 - kH, Vec2{510.0, 0.0});
    EXPECT_EQ(edge.enemies()[0].phase, ChargerPhase::kRecovery);
    EXPECT_EQ(edge.enemies()[0].position.x, 520.0);
    EXPECT_TRUE(std::isfinite(edge.enemies()[0].position.x));
    EXPECT_GE(edge.enemies()[0].position.x, -520.0);
    EXPECT_LE(edge.enemies()[0].position.x, 520.0);
}

TEST(EnemyVariety, ChargerPhaseFrozenOutsidePlaying) {
    CombatWorld world(CombatStats(), 7, quiet_config());
    GameStateManager states;
    ASSERT_TRUE(states.start_run());
    ASSERT_TRUE(world.spawn_enemy(charger_spec(Vec2{-200.0, 0.0})));
    world.tick(states, kH, Vec2{0.0, 0.0});
    ASSERT_EQ(world.enemies()[0].phase, ChargerPhase::kWindup);
    const double remaining = world.enemies()[0].phase_remaining;
    ASSERT_TRUE(states.pause());
    EXPECT_EQ(world.tick(states, 1.0, Vec2{}), CombatWorld::TickResult::kFrozen);
    EXPECT_EQ(world.enemies()[0].phase_remaining, remaining);
    ASSERT_TRUE(states.resume());
    ASSERT_TRUE(states.begin_upgrade());
    EXPECT_EQ(world.tick(states, 1.0, Vec2{}), CombatWorld::TickResult::kFrozen);
    EXPECT_EQ(world.enemies()[0].phase_remaining, remaining);
    ASSERT_TRUE(states.complete_upgrade());
    ASSERT_TRUE(states.lose());
    EXPECT_EQ(world.tick(states, 1.0, Vec2{}), CombatWorld::TickResult::kFrozen);
    EXPECT_EQ(world.enemies()[0].phase_remaining, remaining);
}

TEST(EnemyVariety, RadiusAffectsActualContact) {
    // Distance 70 from the player: outside 54 + 14, inside 54 + 28.
    for (const double r : {14.0, 28.0}) {
        CombatStats stats;
        CombatWorld world(stats, 3, quiet_config());
        GameStateManager states;
        ASSERT_TRUE(states.start_run());
        EnemySpawn s;
        s.position = Vec2{70.0, 0.0};
        s.speed = 0.0;
        s.hp = 1e6;
        s.contact_damage = 10.0;
        s.radius = r;
        ASSERT_TRUE(world.spawn_enemy(s));
        world.tick(states, kH, Vec2{0.0, 0.0});
        if (r == 14.0) {
            EXPECT_EQ(world.hp(), world.max_hp());
        } else {
            EXPECT_LT(world.hp(), world.max_hp());
        }
    }
}

TEST(EnemyVariety, WaveCompositionIntroductionAndCap) {
    const auto run = [](double start, double seconds, std::uint64_t seed) {
        WaveDirector waves;
        waves.configure(CombatConfig());
        EnemyManager m;
        CombatRng rng(seed);
        std::map<EnemyKind, int> counts;
        double t = start;
        for (; t < start + seconds; t += 0.1) {
            waves.step(0.1, t, m, rng);
            for (const Enemy &e : m.enemies()) counts[e.kind] += 0; // touch
            if (m.enemies().size() >= 200) {
                for (const Enemy &e : m.enemies()) ++counts[e.kind];
                m.clear();
            }
        }
        for (const Enemy &e : m.enemies()) ++counts[e.kind];
        EXPECT_LE(m.enemies().size(), EnemyManager::kMaxEnemies);
        return counts;
    };
    const auto early = run(0.0, 44.0, 5);
    EXPECT_EQ(early.count(EnemyKind::kCharger) ? early.at(EnemyKind::kCharger) : 0, 0);
    EXPECT_EQ(early.count(EnemyKind::kBrute) ? early.at(EnemyKind::kBrute) : 0, 0);
    const auto mid = run(45.0, 40.0, 5);
    EXPECT_GT(mid.at(EnemyKind::kCharger), 0);
    EXPECT_EQ(mid.count(EnemyKind::kBrute) ? mid.at(EnemyKind::kBrute) : 0, 0);
    const auto late = run(300.0, 300.0, 5);
    int total = 0;
    for (const auto &kv : late) total += kv.second;
    const double chargers = late.at(EnemyKind::kCharger) / static_cast<double>(total);
    const double brutes = late.at(EnemyKind::kBrute) / static_cast<double>(total);
    EXPECT_NEAR(chargers, 0.20, 0.06);
    EXPECT_NEAR(brutes, 0.15, 0.06);
    const auto again = run(300.0, 300.0, 5);
    EXPECT_EQ(again, late); // same seed, same mixture
}

TEST(EnemyVariety, WaveSpecStatsPerKindAppliedOnce) {
    WaveDirector waves;
    waves.configure(CombatConfig());
    EnemyManager m;
    CombatRng rng(11);
    const double elapsed = 600.0;
    for (int i = 0; i < 400 && m.enemies().size() < 100; ++i) {
        waves.step(1.0, elapsed, m, rng);
    }
    const double hp = 20.0 + elapsed * 0.05;
    const double speed = 70.0 + std::min(50.0, elapsed * 0.04);
    bool chaser = false, charger = false, brute = false;
    for (const Enemy &e : m.enemies()) {
        if (e.kind == EnemyKind::kChaser) {
            chaser = true;
            EXPECT_NEAR(e.max_hp, hp, 1e-9);
            EXPECT_NEAR(e.speed, speed, 1e-9);
            EXPECT_EQ(e.radius, CombatConfig().enemy_radius);
            EXPECT_EQ(e.xp_value, 5.0);
        } else if (e.kind == EnemyKind::kCharger) {
            charger = true;
            EXPECT_NEAR(e.max_hp, hp * 0.75, 1e-9);
            EXPECT_NEAR(e.speed, speed * 1.1, 1e-9);
            EXPECT_EQ(e.radius, 14.0);
            EXPECT_NEAR(e.xp_value, 6.25, 1e-9);
        } else {
            brute = true;
            EXPECT_NEAR(e.max_hp, hp * 3.0, 1e-9);
            EXPECT_NEAR(e.speed, speed * 0.55, 1e-9);
            EXPECT_EQ(e.radius, 28.0);
            EXPECT_NEAR(e.xp_value, 12.0, 1e-9);
        }
    }
    EXPECT_TRUE(chaser && charger && brute);
}

TEST(EnemyVariety, KillEventsCarryPerKindXp) {
    CombatWorld world(CombatStats(), 9, quiet_config());
    GameStateManager states;
    ASSERT_TRUE(states.start_run());
    EnemySpawn s;
    s.hp = 1.0;
    s.speed = 0.0;
    s.contact_damage = 0.0;
    s.position = Vec2{150.0, 0.0};
    s.xp_value = 5.0;
    ASSERT_TRUE(world.spawn_enemy(s));
    s.kind = EnemyKind::kBrute;
    s.radius = 28.0;
    s.xp_value = 12.0;
    s.position = Vec2{-150.0, 0.0};
    ASSERT_TRUE(world.spawn_enemy(s));
    for (int i = 0; i < 300 && world.kill_count() < 2; ++i) {
        world.tick(states, kH, Vec2{0.0, 0.0});
    }
    ASSERT_EQ(world.kill_count(), 2u);
    double total = 0.0;
    for (const KillEvent &k : world.take_kill_events()) total += k.xp;
    EXPECT_NEAR(total, 17.0, 1e-9);
}
