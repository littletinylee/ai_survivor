#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/combat.h"
#include "core/game_timer.h"
#include "core/passive.h"
#include "core/player_controller.h"
#include "core/progression.h"
#include "core/run_step.h"

using namespace ai_survivors;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

CharacterDefinition fish(const std::string &passive = "free_rice") {
    CharacterDefinition c;
    c.id = "test_fish";
    c.name = "Fish";
    c.description = "d";
    c.max_hp = 100.0;
    c.move_speed = 200.0;
    c.pickup_radius = 80.0;
    c.initial_weapon_id = "bubble_shot";
    c.passive_id = passive;
    c.passive_description = "p";
    return c;
}

// Collects `xp` raw orb XP at the origin through the real session path.
double collect(ProgressionSession &s, double xp, GameState state = GameState::kPlaying) {
    EXPECT_TRUE(s.experience().spawn(Vec2{}, xp));
    const double before = s.passive().progress() +
                          100.0 * static_cast<double>(s.passive().proc_count());
    s.ingest_and_collect(state, {}, Vec2{});
    return s.passive().progress() +
           100.0 * static_cast<double>(s.passive().proc_count()) - before;
}

struct Rig {
    GameStateManager states;
    GameTimer timer{states};
    WeaponRegistry weapons = WeaponRegistry::make_default();
    CharacterDefinition character;
    std::unique_ptr<ProgressionSession> progression;
    PlayerController player{520.0, 290.0, 54.0, 220.0};
    std::unique_ptr<CombatWorld> world;

    explicit Rig(CharacterDefinition c = fish()) : character(c) {
        progression = std::make_unique<ProgressionSession>(character, weapons, 7);
        CombatConfig cfg;
        cfg.waves_enabled = false;
        world = std::make_unique<CombatWorld>(CombatStats::from_character(character),
                                              3, cfg);
        states.start_run();
    }
    RunStepResult step(double dt = 1.0 / 60.0) {
        return advance_run(states, timer, player, *world, *progression, dt, 0.0, 0.0);
    }
    // A motionless enemy on top of the player; one contact hit of `damage`.
    void add_biter(double damage) {
        EnemySpawn e;
        e.position = Vec2{10.0, 0.0};
        e.hp = 1e6;
        e.speed = 0.0;
        e.contact_damage = damage;
        e.xp_value = 0.0;
        ASSERT_TRUE(world->spawn_enemy(e));
    }
};

} // namespace

// ------------------------------------------------------------------ registry

TEST(PassiveRegistry, DefaultsAndValidation) {
    const PassiveRegistry r = PassiveRegistry::make_default();
    ASSERT_NE(r.find("none"), nullptr);
    EXPECT_EQ(r.find("none")->kind, PassiveKind::kNone);
    const PassiveSpec *rice = r.find("free_rice");
    ASSERT_NE(rice, nullptr);
    EXPECT_EQ(rice->kind, PassiveKind::kXpHeal);
    EXPECT_DOUBLE_EQ(rice->xp_per_proc, 100.0);
    EXPECT_DOUBLE_EQ(rice->heal_fraction, 0.03);
    EXPECT_EQ(r.find("deep_dive"), nullptr);

    PassiveRegistry copy = r;
    PassiveSpec bad = *rice;
    bad.id = "bad";
    for (double v : {0.0, -1.0, kNaN, kInf, 2e6}) {
        bad.xp_per_proc = v;
        EXPECT_FALSE(copy.add(bad).empty()) << v;
    }
    bad.xp_per_proc = 100.0;
    for (double v : {0.0, -0.1, kNaN, kInf, 1.5}) {
        bad.heal_fraction = v;
        EXPECT_FALSE(copy.add(bad).empty()) << v;
    }
    EXPECT_FALSE(copy.add(*rice).empty()); // duplicate id
    bad = *rice;
    bad.id = "Bad Id";
    EXPECT_FALSE(copy.add(bad).empty());
    PassiveSpec invalid_none;
    invalid_none.id = "invalid_none";
    invalid_none.xp_per_proc = kNaN;
    EXPECT_FALSE(copy.add(invalid_none).empty());
}

// ------------------------------------------------------------------- runtime

TEST(PassiveRuntime, ThresholdAndRemainder) {
    PassiveRuntime rt(*PassiveRegistry::make_default().find("free_rice"));
    EXPECT_EQ(rt.feed(99.0), 0u);
    EXPECT_EQ(rt.pending_procs(), 0u);
    EXPECT_DOUBLE_EQ(rt.progress(), 99.0);
    EXPECT_EQ(rt.feed(1.0), 1u);
    EXPECT_DOUBLE_EQ(rt.progress(), 0.0);
    EXPECT_EQ(rt.feed(30.0), 0u);
    EXPECT_EQ(rt.feed(80.0), 1u); // remainder carried: 30 + 80 = 110
    EXPECT_DOUBLE_EQ(rt.progress(), 10.0);
    EXPECT_NEAR(rt.progress_fraction(), 0.1, 1e-12);
    EXPECT_EQ(rt.proc_count(), 2u);
}

TEST(PassiveRuntime, MultipleThresholdsInOnePickup) {
    PassiveRuntime rt(*PassiveRegistry::make_default().find("free_rice"));
    EXPECT_EQ(rt.feed(250.0), 2u);
    EXPECT_DOUBLE_EQ(rt.progress(), 50.0);
    EXPECT_EQ(rt.pending_procs(), 2u);
    EXPECT_NEAR(rt.take_heal(100.0), 6.0, 1e-12);
    EXPECT_EQ(rt.pending_procs(), 0u);
    EXPECT_EQ(rt.take_heal(100.0), 0.0); // queue was cleared
}

TEST(PassiveRuntime, ExtremeAndHostileInputsStayBounded) {
    PassiveRuntime rt(*PassiveRegistry::make_default().find("free_rice"));
    EXPECT_EQ(rt.feed(kNaN), 0u);
    EXPECT_EQ(rt.feed(kInf), 0u);
    EXPECT_EQ(rt.feed(-5.0), 0u);
    EXPECT_EQ(rt.feed(0.0), 0u);
    EXPECT_EQ(rt.proc_count(), 0u);
    const std::uint32_t got = rt.feed(1.7e308);
    EXPECT_EQ(got, PassiveRuntime::kMaxProcsPerFeed);
    EXPECT_TRUE(std::isfinite(rt.progress()));
    EXPECT_GE(rt.progress(), 0.0);
    EXPECT_LT(rt.progress(), 100.0);
    for (int i = 0; i < 5; ++i) {
        rt.feed(1e300);
    }
    EXPECT_LE(rt.pending_procs(), PassiveRuntime::kMaxProcsPerFeed);
    EXPECT_TRUE(std::isfinite(rt.take_heal(1e6)));
    EXPECT_EQ(rt.take_heal(kNaN), 0.0);
}

TEST(PassiveRuntime, NonePassiveNeverActs) {
    PassiveRuntime rt(*PassiveRegistry::make_default().find("none"));
    EXPECT_EQ(rt.feed(1000.0), 0u);
    EXPECT_EQ(rt.proc_count(), 0u);
    EXPECT_EQ(rt.take_heal(100.0), 0.0);
    EXPECT_EQ(rt.threshold(), 0.0);
    EXPECT_EQ(rt.progress_fraction(), 0.0);
}

TEST(PassiveRuntime, LargeFiniteHealthRequestDoesNotOverflow) {
    PassiveRuntime rt(*PassiveRegistry::make_default().find("free_rice"));
    rt.feed(100000.0);
    const double hp = std::numeric_limits<double>::max();
    EXPECT_DOUBLE_EQ(rt.take_heal(hp), hp);
    EXPECT_EQ(rt.pending_procs(), 0u);
}

TEST(PassiveRuntime, SaturatedCounterDoesNotLoseSmallHealingContributions) {
    PassiveSpec spec;
    spec.id = "small_heal";
    spec.kind = PassiveKind::kXpHeal;
    spec.xp_per_proc = 100.0;
    spec.heal_fraction = 1e-11;
    ASSERT_TRUE(PassiveRegistry::validate(spec).empty());
    PassiveRuntime rt(spec);
    rt.feed(1e12); // ten billion thresholds: display count saturates
    EXPECT_EQ(rt.proc_count(), UINT32_MAX);
    EXPECT_NEAR(rt.take_heal(100.0), 10.0, 1e-12);
    EXPECT_EQ(rt.take_heal(100.0), 0.0);
}

// ------------------------------------------------------------------- session

TEST(PassiveSession, UnknownPassiveFailsInitialization) {
    const ProgressionSession s(fish("deep_dive"), WeaponRegistry::make_default(), 1);
    EXPECT_NE(s.init_error().find("unknown passive"), std::string::npos);
    EXPECT_NE(s.init_error().find("deep_dive"), std::string::npos);
    const ProgressionSession ok(fish("none"), WeaponRegistry::make_default(), 1);
    EXPECT_TRUE(ok.init_error().empty());
}

TEST(PassiveSession, MultiplierAppliesBeforeFeeding) {
    CharacterDefinition c = fish();
    c.xp_multiplier = 2.0;
    ProgressionSession s(c, WeaponRegistry::make_default(), 1);
    collect(s, 49.0); // 98 actual
    EXPECT_EQ(s.passive().proc_count(), 0u);
    collect(s, 1.0); // 100 actual
    EXPECT_EQ(s.passive().proc_count(), 1u);
    EXPECT_DOUBLE_EQ(s.passive().progress(), 0.0);
}

TEST(PassiveSession, DirectGrantsAreNotFood) {
    ProgressionSession s(fish(), WeaponRegistry::make_default(), 1);
    EXPECT_GE(s.grant_xp(GameState::kPlaying, 1000.0), 1);
    EXPECT_EQ(s.passive().proc_count(), 0u);
    EXPECT_EQ(s.passive().progress(), 0.0);
    EXPECT_EQ(s.take_passive_heal(GameState::kPlaying, 100.0), 0.0);
}

TEST(PassiveSession, OnlyPlayingAdvances) {
    ProgressionSession s(fish(), WeaponRegistry::make_default(), 1);
    for (GameState st : {GameState::kMenu, GameState::kPaused, GameState::kUpgrade,
                         GameState::kWon, GameState::kLost}) {
        ASSERT_TRUE(s.experience().spawn(Vec2{}, 500.0));
        EXPECT_EQ(s.ingest_and_collect(st, {}, Vec2{}), 0);
        EXPECT_EQ(s.passive().proc_count(), 0u);
        EXPECT_EQ(s.passive().progress(), 0.0);
        s.experience().clear();
    }
    collect(s, 100.0);
    EXPECT_EQ(s.passive().pending_procs(), 1u);
    // A frozen state neither applies nor discards the queued proc.
    EXPECT_EQ(s.take_passive_heal(GameState::kPaused, 100.0), 0.0);
    EXPECT_EQ(s.take_passive_heal(GameState::kUpgrade, 100.0), 0.0);
    EXPECT_EQ(s.passive().pending_procs(), 1u);
    EXPECT_NEAR(s.take_passive_heal(GameState::kPlaying, 200.0), 6.0, 1e-12);
}

TEST(PassiveSession, FreshSessionResets) {
    ProgressionSession a(fish(), WeaponRegistry::make_default(), 1);
    collect(a, 150.0);
    ASSERT_EQ(a.passive().proc_count(), 1u);
    ProgressionSession b(fish(), WeaponRegistry::make_default(), 1);
    EXPECT_EQ(b.passive().proc_count(), 0u);
    EXPECT_EQ(b.passive().progress(), 0.0);
    EXPECT_EQ(b.passive().pending_procs(), 0u);
}

// -------------------------------------------------------------- combat heal

TEST(CombatHeal, ClampsAndRejects) {
    Rig rig;
    rig.add_biter(10.0);
    rig.step();
    const double hurt = rig.world->hp();
    ASSERT_LT(hurt, rig.world->max_hp());

    for (double bad : {0.0, -3.0, kNaN, kInf, -kInf}) {
        EXPECT_EQ(rig.world->heal(GameState::kPlaying, bad), 0.0);
    }
    for (GameState st : {GameState::kMenu, GameState::kPaused, GameState::kUpgrade,
                         GameState::kWon, GameState::kLost}) {
        EXPECT_EQ(rig.world->heal(st, 5.0), 0.0);
    }
    EXPECT_EQ(rig.world->hp(), hurt);

    EXPECT_NEAR(rig.world->heal(GameState::kPlaying, 4.0), 4.0, 1e-12);
    EXPECT_NEAR(rig.world->hp(), hurt + 4.0, 1e-12);
    // Clamp: reports only what was actually restored.
    const double missing = rig.world->max_hp() - rig.world->hp();
    EXPECT_NEAR(rig.world->heal(GameState::kPlaying, 1e9), missing, 1e-9);
    EXPECT_EQ(rig.world->hp(), rig.world->max_hp());
    EXPECT_EQ(rig.world->heal(GameState::kPlaying, 5.0), 0.0);
}

TEST(CombatHeal, DeadPlayerCannotBeHealed) {
    CharacterDefinition c = fish();
    c.max_hp = 10.0;
    Rig rig(c);
    rig.add_biter(50.0);
    EXPECT_EQ(rig.step().status, RunStepStatus::kLost);
    ASSERT_EQ(rig.world->hp(), 0.0);
    EXPECT_EQ(rig.world->heal(GameState::kPlaying, 5.0), 0.0);
    EXPECT_EQ(rig.world->hp(), 0.0);
}

// --------------------------------------------------------- shared advance_run

TEST(PassiveRun, PickupHealsThroughAdvanceRun) {
    Rig base;
    base.add_biter(10.0);
    base.step();

    Rig fed;
    fed.add_biter(10.0);
    ASSERT_TRUE(fed.progression->experience().spawn(Vec2{}, 100.0));
    fed.step();
    EXPECT_NEAR(fed.world->hp() - base.world->hp(), 3.0, 1e-9);
    EXPECT_EQ(fed.progression->passive().proc_count(), 1u);
    EXPECT_EQ(fed.progression->passive().pending_procs(), 0u);

    Rig multi;
    multi.add_biter(10.0);
    ASSERT_TRUE(multi.progression->experience().spawn(Vec2{}, 250.0));
    multi.step();
    EXPECT_NEAR(multi.world->hp() - base.world->hp(), 6.0, 1e-9);
    EXPECT_DOUBLE_EQ(multi.progression->passive().progress(), 50.0);
}

TEST(PassiveRun, HealHappensBeforeUpgradeOffer) {
    Rig rig;
    rig.add_biter(10.0);
    ASSERT_TRUE(rig.progression->experience().spawn(Vec2{}, 100.0));
    const RunStepResult r = rig.step();
    // 100 XP crosses many levels, so the frame ends in Upgrade...
    EXPECT_EQ(rig.states.state(), GameState::kUpgrade);
    EXPECT_EQ(r.upgrade, UpgradePrep::kOfferActive);
    // ...but the proc was already spent in that same frame.
    EXPECT_NEAR(rig.world->hp(), 93.0, 1e-9);
    EXPECT_EQ(rig.progression->passive().pending_procs(), 0u);
}

TEST(PassiveRun, FullHpProcIsNotBanked) {
    Rig rig;
    ASSERT_TRUE(rig.progression->experience().spawn(Vec2{}, 100.0));
    rig.step();
    ASSERT_EQ(rig.progression->passive().proc_count(), 1u);
    EXPECT_EQ(rig.world->hp(), rig.world->max_hp());
    EXPECT_EQ(rig.progression->passive().pending_procs(), 0u);

    // Get hurt later with no new food: nothing was saved for this.
    rig.states.complete_upgrade();
    rig.add_biter(10.0);
    rig.progression->experience().clear();
    rig.step();
    EXPECT_NEAR(rig.world->hp(), 90.0, 1e-9);
}

TEST(PassiveRun, PauseAndUpgradeFreezeFood) {
    Rig rig;
    rig.add_biter(10.0);
    ASSERT_TRUE(rig.progression->experience().spawn(Vec2{}, 100.0));
    ASSERT_TRUE(rig.states.pause());
    EXPECT_EQ(rig.step().status, RunStepStatus::kFrozen);
    EXPECT_EQ(rig.progression->passive().proc_count(), 0u);
    EXPECT_EQ(rig.world->hp(), rig.world->max_hp());
    EXPECT_EQ(rig.progression->experience().orbs().size(), 1u);
    ASSERT_TRUE(rig.states.resume());

    rig.step(); // now it runs: orb collected, hit taken, heal applied
    ASSERT_EQ(rig.states.state(), GameState::kUpgrade);
    const double hp = rig.world->hp();
    const std::uint32_t procs = rig.progression->passive().proc_count();
    EXPECT_EQ(rig.step().status, RunStepStatus::kFrozen);
    EXPECT_EQ(rig.world->hp(), hp);
    EXPECT_EQ(rig.progression->passive().proc_count(), procs);
}

TEST(PassiveRun, LethalHitInSameFrameIsNotRevived) {
    CharacterDefinition c = fish();
    c.max_hp = 10.0;
    Rig rig(c);
    rig.add_biter(50.0);
    ASSERT_TRUE(rig.progression->experience().spawn(Vec2{}, 1000.0));
    const RunStepResult r = rig.step();
    EXPECT_EQ(r.status, RunStepStatus::kLost);
    EXPECT_EQ(rig.states.state(), GameState::kLost);
    EXPECT_EQ(rig.world->hp(), 0.0);
    EXPECT_EQ(rig.progression->passive().proc_count(), 0u);
    EXPECT_EQ(rig.progression->experience().orbs().size(), 1u);
}

TEST(PassiveRun, NoPassiveCharacterGetsNoHealing) {
    Rig rig(fish("none"));
    rig.add_biter(10.0);
    ASSERT_TRUE(rig.progression->experience().spawn(Vec2{}, 500.0));
    rig.step();
    EXPECT_NEAR(rig.world->hp(), 90.0, 1e-9);
    EXPECT_EQ(rig.progression->passive().proc_count(), 0u);
}
