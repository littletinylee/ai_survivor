#include <cmath>
#include <limits>
#include <memory>
#include <tuple>

#include <gtest/gtest.h>

#include "core/combat.h"
#include "core/game_timer.h"

using namespace ai_survivors;

namespace {

constexpr double kStep = 1.0 / 60.0;

CombatConfig manual_config() {
    CombatConfig c;
    c.waves_enabled = false;
    return c;
}

EnemySpawn enemy_at(double x, double y, double hp = 20.0) {
    EnemySpawn s;
    s.position = Vec2{x, y};
    s.hp = hp;
    s.speed = 0.0;
    s.contact_damage = 10.0;
    s.xp_value = 7.0;
    return s;
}

struct Fixture {
    GameStateManager states;
    CombatStats stats;
    std::unique_ptr<CombatWorld> world;

    explicit Fixture(CombatConfig config = manual_config(),
                     std::uint64_t seed = 42) {
        states.start_run();
        world = std::make_unique<CombatWorld>(stats, seed, config);
    }
    CombatWorld::TickResult tick(double dt = kStep, Vec2 p = Vec2{}) {
        return world->tick(states, dt, p);
    }
};

} // namespace

TEST(DamageSystem, ArmorReducesAndKeepsMinimumHit) {
    EXPECT_DOUBLE_EQ(DamageSystem::mitigate(10.0, 0.0), 10.0);
    EXPECT_DOUBLE_EQ(DamageSystem::mitigate(10.0, 100.0), 5.0);
    // Armor is capped at 1000 (10 * 100/1100 < 1), so the minimum hit applies.
    EXPECT_DOUBLE_EQ(DamageSystem::mitigate(10.0, 1e9), 1.0);
    EXPECT_DOUBLE_EQ(DamageSystem::mitigate(0.0, 5.0), 0.0);
    EXPECT_DOUBLE_EQ(DamageSystem::mitigate(std::nan(""), 5.0), 0.0);
    EXPECT_DOUBLE_EQ(DamageSystem::mitigate(10.0, -50.0), 10.0);
}

TEST(DamageSystem, MultiplierAndCritLimits) {
    EXPECT_DOUBLE_EQ(DamageSystem::outgoing_damage(10.0, 2.0, false), 20.0);
    EXPECT_DOUBLE_EQ(DamageSystem::outgoing_damage(10.0, 2.0, true), 40.0);
    EXPECT_DOUBLE_EQ(DamageSystem::outgoing_damage(10.0, 1e6, false),
                     10.0 * DamageSystem::kMaxDamageMultiplier);
    EXPECT_DOUBLE_EQ(DamageSystem::outgoing_damage(10.0, -3.0, false), 0.0);

    CombatRng rng(1);
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(DamageSystem::roll_crit(0.0, rng));
        EXPECT_TRUE(DamageSystem::roll_crit(5.0, rng)); // clamped to 1
        EXPECT_FALSE(DamageSystem::roll_crit(-1.0, rng));
    }
}

TEST(WeaponManager, AutoAttackTargetsNearestAndRespectsCooldown) {
    Fixture f;
    f.world->spawn_enemy(enemy_at(300.0, 0.0));
    f.world->spawn_enemy(enemy_at(0.0, -150.0));
    f.tick();
    ASSERT_EQ(f.world->projectiles().size(), 1u);
    const Projectile &p = f.world->projectiles()[0];
    EXPECT_LT(p.velocity.y, 0.0); // aimed at the nearer enemy (above)
    EXPECT_NEAR(p.velocity.x, 0.0, 1e-9);
    EXPECT_FALSE(p.critical);

    // Cooldown 0.8 s: after 0.5 s only the first shot has landed (12 of 20 hp).
    for (int i = 0; i < 30; ++i) f.tick();
    EXPECT_DOUBLE_EQ(f.world->enemies()[1].hp, 8.0);
    EXPECT_TRUE(f.world->projectiles().empty());
    // Past 0.8 s the second shot kills it (plus ~0.22 s of flight).
    for (int i = 0; i < 50; ++i) f.tick();
    EXPECT_EQ(f.world->kill_count(), 1u);
}

TEST(WeaponManager, AttackSpeedShortensInterval) {
    WeaponManager slow, fast;
    CombatStats s;
    CombatConfig c;
    slow.configure(s, c);
    s.attack_speed_multiplier = 2.0;
    fast.configure(s, c);
    EXPECT_DOUBLE_EQ(fast.interval() * 2.0, slow.interval());
}

TEST(WeaponManager, NoTargetOutOfRangeMeansNoShot) {
    Fixture f;
    f.world->spawn_enemy(enemy_at(5000.0, 0.0));
    f.tick();
    EXPECT_TRUE(f.world->projectiles().empty());
}

TEST(Projectile, HitDamagesOnceAndRetires) {
    Fixture f;
    f.world->spawn_enemy(enemy_at(100.0, 0.0, 1000.0));
    for (int i = 0; i < 20 && f.world->enemies()[0].hp >= 1000.0; ++i) f.tick();
    ASSERT_EQ(f.world->enemies().size(), 1u);
    EXPECT_DOUBLE_EQ(f.world->enemies()[0].hp, 1000.0 - 12.0);
    EXPECT_TRUE(f.world->projectiles().empty());
    f.tick();
    EXPECT_DOUBLE_EQ(f.world->enemies()[0].hp, 1000.0 - 12.0); // no re-hit
}

TEST(Projectile, MissedProjectileExpires) {
    ProjectileSystem sys;
    Projectile p;
    p.remaining_life = 0.1;
    p.velocity = Vec2{1.0, 0.0};
    sys.spawn(p);
    for (int i = 0; i < 10; ++i) sys.step(kStep);
    EXPECT_TRUE(sys.projectiles().empty());
}

TEST(Combat, KillRemovesEnemyAndEmitsEventOnce) {
    Fixture f;
    f.world->spawn_enemy(enemy_at(100.0, 0.0, 5.0));
    for (int i = 0; i < 30 && f.world->kill_count() == 0; ++i) f.tick();
    EXPECT_EQ(f.world->kill_count(), 1u);
    EXPECT_TRUE(f.world->enemies().empty());
    const std::vector<KillEvent> events = f.world->take_kill_events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_DOUBLE_EQ(events[0].xp, 7.0);
    EXPECT_NEAR(events[0].position.x, 100.0, 1e-9);
    EXPECT_TRUE(f.world->pending_kill_events().empty());
}

TEST(EnemyManager, PursuesTargetWithoutOvershoot) {
    EnemyManager m;
    EnemySpawn s = enemy_at(100.0, 0.0);
    s.speed = 60.0;
    ASSERT_TRUE(m.spawn(s, 18.0));
    m.step(1.0, Vec2{0.0, 0.0});
    EXPECT_NEAR(m.enemies()[0].position.x, 40.0, 1e-9);
    m.step(1.0, Vec2{0.0, 0.0});
    EXPECT_NEAR(m.enemies()[0].position.x, 0.0, 1e-9);
}

TEST(WaveDirector, SpawnsOnArenaBoundaryDeterministically) {
    CombatConfig c;
    auto spawn_positions = [&](std::uint64_t seed) {
        GameStateManager st;
        st.start_run();
        CombatStats stats;
        stats.max_hp = 1e6;
        CombatWorld w(stats, seed, c);
        for (int i = 0; i < 300; ++i) w.tick(st, kStep, Vec2{9999.0, 9999.0});
        return w.enemies();
    };
    const auto a = spawn_positions(7);
    const auto b = spawn_positions(7);
    ASSERT_FALSE(a.empty());
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].position.x, b[i].position.x);
        EXPECT_EQ(a[i].position.y, b[i].position.y);
    }
    // Spawn position check on a fresh world, before any movement matters.
    GameStateManager st;
    st.start_run();
    CombatWorld w(CombatStats(), 3, c);
    w.tick(st, kStep, Vec2{});
    ASSERT_FALSE(w.enemies().empty());
    const Vec2 p = w.enemies()[0].position;
    const bool on_edge = std::abs(std::abs(p.x) - c.arena_half_width) < 5.0 ||
                         std::abs(std::abs(p.y) - c.arena_half_height) < 5.0;
    EXPECT_TRUE(on_edge);
}

TEST(Combat, PauseAndUpgradeFreezeEverything) {
    Fixture f;
    f.world->spawn_enemy(enemy_at(200.0, 0.0, 1000.0));
    f.tick();
    const std::size_t shots = f.world->projectiles().size();
    const Vec2 shot_pos = f.world->projectiles()[0].position;
    const double t = f.world->combat_time();

    ASSERT_TRUE(f.states.pause());
    EXPECT_EQ(f.tick(1.0), CombatWorld::TickResult::kFrozen);
    ASSERT_TRUE(f.states.resume());
    ASSERT_TRUE(f.states.begin_upgrade());
    EXPECT_EQ(f.tick(1.0), CombatWorld::TickResult::kFrozen);
    EXPECT_EQ(f.world->projectiles().size(), shots);
    EXPECT_EQ(f.world->projectiles()[0].position.x, shot_pos.x);
    EXPECT_EQ(f.world->combat_time(), t);
}

TEST(Combat, ContactDamageUsesCooldownNotEveryFrame) {
    Fixture f;
    CombatStats s;
    s.max_hp = 100.0;
    f.world = std::make_unique<CombatWorld>(s, 1, manual_config());
    f.world->spawn_enemy(enemy_at(0.0, 0.0, 1e6));
    // Standing on an enemy for 2.5 s (150 frames): hits at t=0, 1, 2 only.
    for (int i = 0; i < 150; ++i) f.tick();
    EXPECT_DOUBLE_EQ(f.world->hp(), 70.0);
    EXPECT_EQ(f.states.state(), GameState::kPlaying);
}

TEST(Combat, ContactDamageIsFrameRateIndependent) {
    auto hp_after = [](double dt, int frames) {
        GameStateManager st;
        st.start_run();
        CombatWorld w(CombatStats(), 1, manual_config());
        w.spawn_enemy(enemy_at(0.0, 0.0, 1e6));
        for (int i = 0; i < frames; ++i) w.tick(st, dt, Vec2{});
        return w.hp();
    };
    EXPECT_DOUBLE_EQ(hp_after(1.0 / 30.0, 75), hp_after(1.0 / 120.0, 300));
}

TEST(Combat, ArmorReducesContactDamage) {
    GameStateManager st;
    st.start_run();
    CombatStats s;
    s.armor = 100.0;
    CombatWorld w(s, 1, manual_config());
    w.spawn_enemy(enemy_at(0.0, 0.0, 1e6));
    w.tick(st, kStep, Vec2{});
    EXPECT_DOUBLE_EQ(w.hp(), 95.0);
}

TEST(Combat, DeathLosesAndFreezesTerminal) {
    GameStateManager st;
    st.start_run();
    CombatStats s;
    s.max_hp = 5.0;
    CombatWorld w(s, 1, manual_config());
    w.spawn_enemy(enemy_at(0.0, 0.0, 1e6));
    EXPECT_EQ(w.tick(st, kStep, Vec2{}), CombatWorld::TickResult::kPlayerDied);
    EXPECT_EQ(st.state(), GameState::kLost);
    EXPECT_DOUBLE_EQ(w.hp(), 0.0);
    const double t = w.combat_time();
    EXPECT_EQ(w.tick(st, 1.0, Vec2{}), CombatWorld::TickResult::kFrozen);
    EXPECT_EQ(w.combat_time(), t);
}

TEST(Combat, LethalHitBeatsVictoryBoundaryWhenCombatRunsFirst) {
    GameStateManager st;
    GameTimer timer(st);
    st.start_run();
    ASSERT_EQ(timer.advance(1199.99), GameTimer::AdvanceResult::kAdvanced);
    CombatStats s;
    s.max_hp = 5.0;
    CombatWorld w(s, 1, manual_config());
    w.spawn_enemy(enemy_at(0.0, 0.0, 1e6));
    // Caller order: combat first, timer only if still Playing.
    w.tick(st, 0.02, Vec2{});
    if (st.state() == GameState::kPlaying) timer.advance(0.02);
    EXPECT_EQ(st.state(), GameState::kLost);
}

TEST(Combat, RejectsInvalidDeltaAndClampsHugeDelta) {
    Fixture f;
    f.world->spawn_enemy(enemy_at(100.0, 0.0, 1e6));
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(f.tick(nan), CombatWorld::TickResult::kRejected);
    EXPECT_EQ(f.tick(-0.1), CombatWorld::TickResult::kRejected);
    EXPECT_EQ(f.tick(inf), CombatWorld::TickResult::kRejected);
    EXPECT_EQ(f.tick(kStep, Vec2{nan, 0.0}), CombatWorld::TickResult::kRejected);
    EXPECT_EQ(f.world->combat_time(), 0.0);
    EXPECT_EQ(f.tick(1e12), CombatWorld::TickResult::kAdvanced);
    EXPECT_NEAR(f.world->combat_time(), CombatWorld::kMaxAcceptedDelta, 1e-9);
}

TEST(Combat, CapsHoldOverLongFixedStepSimulation) {
    GameStateManager st;
    st.start_run();
    CombatStats s;
    s.max_hp = 1e6; // survive so the whole run exercises the caps
    s.damage_multiplier = 0.0; // enemies never die -> enemy cap pressure
    CombatWorld w(s, 99, CombatConfig());
    for (int i = 0; i < 60 * 600; ++i) {
        w.tick(st, kStep, Vec2{});
        ASSERT_LE(w.enemies().size(), EnemyManager::kMaxEnemies);
        ASSERT_LE(w.projectiles().size(), ProjectileSystem::kMaxProjectiles);
        ASSERT_LE(w.pending_kill_events().size(), CombatWorld::kMaxKillEvents);
    }
    EXPECT_EQ(w.enemies().size(), EnemyManager::kMaxEnemies);
}

TEST(Combat, KillEventBufferIsBounded) {
    GameStateManager st;
    st.start_run();
    CombatStats s;
    s.max_hp = 1e6;
    s.damage_multiplier = 10.0;
    CombatWorld w(s, 5, CombatConfig());
    for (int i = 0; i < 60 * 900; ++i) {
        w.tick(st, kStep, Vec2{});
        ASSERT_LE(w.pending_kill_events().size(), CombatWorld::kMaxKillEvents);
    }
    EXPECT_GT(w.kill_count(), CombatWorld::kMaxKillEvents);
    EXPECT_EQ(w.dropped_kill_events(),
              w.kill_count() - CombatWorld::kMaxKillEvents);
}

TEST(Combat, SameSeedSameOutcome) {
    auto run = [](std::uint64_t seed) {
        GameStateManager st;
        st.start_run();
        CombatStats s;
        s.crit_chance = 0.3;
        CombatWorld w(s, seed, CombatConfig());
        for (int i = 0; i < 60 * 120; ++i) w.tick(st, kStep, Vec2{});
        return std::make_tuple(w.hp(), w.kill_count(), w.enemies().size(),
                               static_cast<int>(st.state()));
    };
    EXPECT_EQ(run(11), run(11));
    EXPECT_NE(run(11), run(12));
}
