#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "core/combat.h"
#include "core/game_timer.h"
#include "core/player_controller.h"
#include "core/progression.h"
#include "core/run_simulation.h"
#include "core/run_step.h"

// These tests exercise the shared advance_run / choose_run_upgrade helpers.
// The controlled-victory fixture uses a custom, deliberately overpowered
// character. It proves the full 1200 s pipeline (waves, projectiles, contact
// damage, XP, cards, caps, win clock) works end to end. It does NOT prove the
// default fish is balanced: the default-character tests only assert
// determinism and invariants, and accept Won or Lost. Normal-mode balance
// (HP/damage/wave curves) remains open work.

namespace {

using namespace ai_survivors;

CharacterDefinition default_fish() {
    CharacterDefinition c; // mirrors data/characters/deepseek_big_fish.json
    c.id = "deepseek_big_fish";
    c.name = "fish";
    c.description = "default fish values";
    c.max_hp = 100.0;
    c.move_speed = 220.0;
    c.crit_chance = 0.05;
    c.pickup_radius = 80.0;
    c.initial_weapon_id = "bubble_shot";
    c.passive_id = "free_rice";
    c.passive_description = "mirrors the fish JSON";
    return c;
}

// SCENARIO: controlled fixture, NOT the default fish.
CharacterDefinition controlled_fixture() {
    CharacterDefinition c = default_fish();
    c.id = "controlled_victory_fixture";
    c.max_hp = 1e6;
    c.pickup_radius = 1000.0;
    return c;
}

void expect_invariants(const SimulationResult &r) {
    EXPECT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.terminated);
    EXPECT_FALSE(r.hit_iteration_limit);
    EXPECT_FALSE(r.stalled);
    EXPECT_TRUE(r.final_state == GameState::kWon ||
                r.final_state == GameState::kLost);
    EXPECT_EQ(r.invalid_selections, 0);
    EXPECT_EQ(r.frozen_violations, 0);
    EXPECT_EQ(r.terminal_violations, 0);
    EXPECT_LE(r.peak_enemies, EnemyManager::kMaxEnemies);
    EXPECT_LE(r.peak_projectiles, ProjectileSystem::kMaxProjectiles);
    EXPECT_LE(r.peak_orbs, ExperienceSystem::kMaxOrbs);
    EXPECT_LE(r.peak_weapons, WeaponInventory::kMaxWeapons);
    EXPECT_LE(r.peak_zones, AreaSink::kMaxZones);
    EXPECT_LE(r.survival_seconds, GameTimer::kWinDurationSeconds);
    if (r.final_state == GameState::kWon) {
        EXPECT_EQ(r.survival_seconds, GameTimer::kWinDurationSeconds);
        EXPECT_GT(r.final_hp, 0.0);
    } else {
        EXPECT_EQ(r.final_hp, 0.0);
        EXPECT_LT(r.survival_seconds, GameTimer::kWinDurationSeconds);
    }
}

// Timeline invariants: bounded, finite, monotone, and the last sample is the
// terminal state. Structure only; says nothing about balance.
void expect_timeline(const SimulationResult &r) {
    ASSERT_GE(r.samples.size(), 2u);
    ASSERT_LE(r.samples.size(), SimulationResult::kMaxSamples);
    EXPECT_EQ(r.samples.front().time, 0.0);
    EXPECT_EQ(r.samples.front().kills, 0u);
    EXPECT_EQ(r.samples.front().health.damage_taken, 0.0);
    for (std::size_t i = 0; i < r.samples.size(); ++i) {
        const SimulationSample &s = r.samples[i];
        for (const double v : {s.time, s.hp, s.max_hp, s.armor, s.xp_gained,
                               s.passive_progress, s.health.damage_taken,
                               s.health.healing_received, s.health.max_hp_gain}) {
            EXPECT_TRUE(std::isfinite(v));
            EXPECT_GE(v, 0.0);
        }
        for (const double v : s.health.contact_damage_by_kind) {
            EXPECT_TRUE(std::isfinite(v));
            EXPECT_GE(v, 0.0);
        }
        EXPECT_LE(s.hp, s.max_hp);
        std::uint32_t hits = 0;
        double kind_damage = 0.0;
        for (const std::uint32_t h : s.health.contact_hits_by_kind) {
            hits += h;
        }
        for (const double d : s.health.contact_damage_by_kind) {
            kind_damage += d;
        }
        EXPECT_EQ(hits, s.health.contact_hits);
        EXPECT_LE(kind_damage, s.health.damage_taken + 1e-6);
        if (i == 0) {
            continue;
        }
        const SimulationSample &p = r.samples[i - 1];
        EXPECT_GT(s.time, p.time) << "sample " << i;
        EXPECT_GE(s.kills, p.kills);
        EXPECT_GE(s.level, p.level);
        EXPECT_GE(s.cards, p.cards);
        EXPECT_GE(s.xp_gained, p.xp_gained);
        EXPECT_GE(s.passive_procs, p.passive_procs);
        EXPECT_GE(s.health.damage_taken, p.health.damage_taken);
        EXPECT_GE(s.health.healing_received, p.health.healing_received);
        EXPECT_GE(s.health.max_hp_gain, p.health.max_hp_gain);
        EXPECT_GE(s.health.contact_hits, p.health.contact_hits);
        for (std::size_t k = 0; k < HealthMetrics::kKindCount; ++k) {
            EXPECT_GE(s.health.contact_hits_by_kind[k],
                      p.health.contact_hits_by_kind[k]);
            EXPECT_GE(s.health.contact_damage_by_kind[k],
                      p.health.contact_damage_by_kind[k]);
        }
    }
    const SimulationSample &last = r.samples.back();
    EXPECT_EQ(last.time, r.survival_seconds);
    EXPECT_EQ(last.hp, r.final_hp);
    EXPECT_EQ(last.max_hp, r.max_hp);
    EXPECT_EQ(last.level, r.final_level);
    EXPECT_EQ(last.cards, r.cards_picked);
    EXPECT_EQ(last.kills, r.kills);
    EXPECT_EQ(last.xp_gained, r.xp_gained);
    // HP offer totals come from real offers and accepted choices only.
    EXPECT_GE(r.hp_offers_seen, r.hp_cards_selected);
    EXPECT_GE(r.hp_offers_seen, r.hp_offers_declined_healthy);
    EXPECT_LE(r.hp_cards_selected + r.hp_offers_declined_healthy, r.hp_offers_seen);
    EXPECT_LE(r.hp_offers_seen, r.upgrade_offers);
}

// Hand-built run for single-frame behaviour tests (waves off).
struct Rig {
    GameStateManager states;
    GameTimer timer{states};
    WeaponRegistry weapons = WeaponRegistry::make_default();
    CharacterDefinition character;
    std::unique_ptr<ProgressionSession> progression;
    PlayerController player{520.0, 290.0, 54.0, 220.0};
    std::unique_ptr<CombatWorld> world;

    explicit Rig(CharacterDefinition c = default_fish()) : character(c) {
        progression = std::make_unique<ProgressionSession>(character, weapons, 7);
        CombatConfig cfg;
        cfg.waves_enabled = false;
        world = std::make_unique<CombatWorld>(CombatStats::from_character(character),
                                              3, cfg);
        states.start_run();
    }
    RunStepResult step(double dt, double ix = 0.0, double iy = 0.0) {
        return advance_run(states, timer, player, *world, *progression, dt, ix, iy);
    }
};

TEST(FullRun, ControlledVictoryFixtureReachesWon) {
    const SimulationResult r = run_simulation(
        controlled_fixture(), 11, SimulationPolicy::kKiteAndCollect);
    expect_invariants(r);
    EXPECT_EQ(r.final_state, GameState::kWon);
    EXPECT_GT(r.final_hp, 0.0);
    EXPECT_GT(r.kills, 0u);
    EXPECT_GT(r.xp_gained, 0.0);
    EXPECT_GT(r.upgrade_offers, 0);
    EXPECT_GT(r.cards_picked, 0);
    EXPECT_GT(r.peak_enemies, 0u);
    EXPECT_GT(r.peak_projectiles, 0u);
}

TEST(FullRun, DefaultFishBotIsDeterministicAndConsistent) {
    for (const std::uint64_t seed : {1ULL, 2ULL}) {
        const SimulationResult a =
            run_simulation(default_fish(), seed, SimulationPolicy::kKiteAndCollect);
        const SimulationResult b =
            run_simulation(default_fish(), seed, SimulationPolicy::kKiteAndCollect);
        expect_invariants(a); // Won or Lost both acceptable; no balance claim
        EXPECT_EQ(a.final_state, b.final_state);
        EXPECT_EQ(a.trace_hash, b.trace_hash);
        EXPECT_EQ(a.survival_seconds, b.survival_seconds);
        EXPECT_EQ(a.kills, b.kills);
        EXPECT_EQ(a.cards_picked, b.cards_picked);
        EXPECT_EQ(a.frames, b.frames);
    }
    const SimulationResult s =
        run_simulation(default_fish(), 1, SimulationPolicy::kStationary);
    expect_invariants(s);
    expect_timeline(s);
}

TEST(FullRun, TimelineIsBoundedMonotoneAndDeterministic) {
    for (const std::uint64_t seed : {1ULL, 2ULL}) {
        const SimulationResult a =
            run_simulation(default_fish(), seed, SimulationPolicy::kKiteAndCollect);
        const SimulationResult b =
            run_simulation(default_fish(), seed, SimulationPolicy::kKiteAndCollect);
        expect_timeline(a);
        ASSERT_EQ(a.samples.size(), b.samples.size());
        for (std::size_t i = 0; i < a.samples.size(); ++i) {
            EXPECT_EQ(a.samples[i].time, b.samples[i].time);
            EXPECT_EQ(a.samples[i].hp, b.samples[i].hp);
            EXPECT_EQ(a.samples[i].health.damage_taken,
                      b.samples[i].health.damage_taken);
        }
        // Periodic samples land at or just after each 60 s mark.
        for (std::size_t i = 1; i + 1 < a.samples.size(); ++i) {
            EXPECT_GE(a.samples[i].time, 60.0 * static_cast<double>(i));
            EXPECT_LT(a.samples[i].time, 60.0 * static_cast<double>(i) + 1.0);
        }
    }
    // A full 1200 s run holds start + 20 marks + terminal at most.
    const SimulationResult won = run_simulation(
        controlled_fixture(), 11, SimulationPolicy::kKiteAndCollect);
    expect_timeline(won);
    EXPECT_LE(won.samples.size(), SimulationResult::kMaxSamples);
    EXPECT_GE(won.samples.size(), 21u);
}

TEST(FullRun, LethalContactLosesThroughSharedHelpers) {
    CharacterDefinition fragile = default_fish();
    fragile.max_hp = 1.0;
    Rig rig(fragile);
    EnemySpawn killer;
    killer.speed = 0.0;
    killer.contact_damage = 50.0;
    ASSERT_TRUE(rig.world->spawn_enemy(killer));
    const RunStepResult r = rig.step(1.0 / 60.0);
    EXPECT_EQ(r.status, RunStepStatus::kLost);
    EXPECT_EQ(rig.states.state(), GameState::kLost);
    EXPECT_EQ(rig.world->hp(), 0.0);
    const double t = rig.timer.elapsed_seconds();
    EXPECT_EQ(rig.step(1.0).status, RunStepStatus::kFrozen);
    EXPECT_EQ(rig.timer.elapsed_seconds(), t);
}

TEST(FullRun, DeltaIsClampedOnceForMovementCombatAndTimer) {
    Rig rig;
    const RunStepResult r = rig.step(5.0, 1.0, 0.0);
    EXPECT_EQ(r.status, RunStepStatus::kAdvanced);
    EXPECT_EQ(r.consumed, CombatWorld::kMaxAcceptedDelta);
    EXPECT_NEAR(rig.timer.elapsed_seconds(), CombatWorld::kMaxAcceptedDelta, 1e-9);
    EXPECT_NEAR(rig.world->combat_time(), CombatWorld::kMaxAcceptedDelta, 1e-9);
    EXPECT_NEAR(rig.player.position().x, 220.0 * CombatWorld::kMaxAcceptedDelta,
                1e-9);
}

TEST(FullRun, InvalidInputIsRejectedWithoutMutation) {
    Rig rig;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(rig.step(nan).status, RunStepStatus::kRejected);
    EXPECT_EQ(rig.step(-0.1).status, RunStepStatus::kRejected);
    EXPECT_EQ(rig.step(0.1, nan, 0.0).status, RunStepStatus::kRejected);
    EXPECT_EQ(rig.timer.elapsed_seconds(), 0.0);
    EXPECT_EQ(rig.world->combat_time(), 0.0);
    EXPECT_EQ(rig.player.position().x, 0.0);
}

TEST(FullRun, PauseAndUpgradeFreezeEverything) {
    Rig rig;
    ASSERT_TRUE(rig.states.pause());
    EXPECT_EQ(rig.step(0.1, 1.0, 0.0).status, RunStepStatus::kFrozen);
    EXPECT_EQ(rig.player.position().x, 0.0);
    EXPECT_EQ(rig.timer.elapsed_seconds(), 0.0);
    ASSERT_TRUE(rig.states.resume());

    ASSERT_GT(rig.progression->grant_xp(GameState::kPlaying, 100.0), 0);
    ASSERT_EQ(prepare_next_upgrade(rig.states, *rig.progression),
              UpgradePrep::kOfferActive);
    ASSERT_EQ(rig.states.state(), GameState::kUpgrade);
    EXPECT_EQ(rig.step(1.0, 1.0, 0.0).status, RunStepStatus::kFrozen);
    EXPECT_EQ(rig.timer.elapsed_seconds(), 0.0);
    EXPECT_EQ(rig.world->combat_time(), 0.0);
    EXPECT_EQ(rig.player.position().x, 0.0);

    // Re-preparing must not replace the active offer.
    const UpgradeOffer offer = rig.progression->cards().current_offer();
    EXPECT_EQ(prepare_next_upgrade(rig.states, *rig.progression),
              UpgradePrep::kOfferActive);
    EXPECT_EQ(rig.progression->cards().current_offer().generation,
              offer.generation);

    const int pending = rig.progression->levels().pending_levels();
    EXPECT_EQ(choose_run_upgrade(rig.states, *rig.world, rig.player,
                                 *rig.progression, offer.generation + 1,
                                 offer.card_ids[0])
                  .result,
              ChooseResult::kStaleGeneration);
    EXPECT_EQ(rig.progression->levels().pending_levels(), pending);
    const ChooseRunResult ok = choose_run_upgrade(
        rig.states, *rig.world, rig.player, *rig.progression, offer.generation,
        offer.card_ids[0]);
    EXPECT_EQ(ok.result, ChooseResult::kApplied);
    EXPECT_EQ(rig.progression->levels().pending_levels(), pending - 1);
    EXPECT_EQ(rig.world->max_hp(), rig.progression->stats().max_hp);
    EXPECT_EQ(rig.player.move_speed(), rig.progression->stats().move_speed);
    if (pending - 1 > 0) {
        EXPECT_EQ(ok.next, UpgradePrep::kOfferActive);
        EXPECT_EQ(rig.states.state(), GameState::kUpgrade);
    } else {
        EXPECT_EQ(rig.states.state(), GameState::kPlaying);
    }
    // Choosing again outside Upgrade (or with no offer) is a no-op.
    while (rig.states.state() == GameState::kUpgrade) {
        const UpgradeOffer next = rig.progression->cards().current_offer();
        ASSERT_EQ(choose_run_upgrade(rig.states, *rig.world, rig.player,
                                     *rig.progression, next.generation,
                                     next.card_ids[0])
                      .result,
                  ChooseResult::kApplied);
    }
    EXPECT_EQ(choose_run_upgrade(rig.states, *rig.world, rig.player,
                                 *rig.progression, 1, "attr_armor")
                  .result,
              ChooseResult::kNoOffer);
}

TEST(FullRun, TerminalSampleReplacesPriorStateAtUnchangedGameTime) {
    SimulationResult report;
    SimulationSample sample;
    sample.hp = sample.max_hp = 100.0;
    report.record_sample(sample);
    sample.time = 60.0;
    sample.hp = 12.0;
    report.record_sample(sample);
    // Combat kills the player on the next frame; GameTimer stays at60.
    sample.hp = 0.0;
    sample.cards = 7;
    report.record_sample(sample);
    ASSERT_EQ(report.samples.size(), 2u);
    EXPECT_LT(report.samples.front().time, report.samples.back().time);
    EXPECT_EQ(report.samples.back().hp, 0.0);
    EXPECT_EQ(report.samples.back().cards, 7);
}

TEST(FullRun, ImmediateTerminalSampleDoesNotDuplicateTimeZero) {
    SimulationResult report;
    SimulationSample sample;
    sample.hp = sample.max_hp = 100.0;
    report.record_sample(sample);
    sample.hp = 0.0;
    report.record_sample(sample);
    ASSERT_EQ(report.samples.size(), 1u);
    EXPECT_EQ(report.samples.back().time, 0.0);
    EXPECT_EQ(report.samples.back().hp, 0.0);
}

TEST(FullRun, SameFrameDeathBeatsVictory) {
    CharacterDefinition fragile = default_fish();
    fragile.max_hp = 1.0;
    Rig rig(fragile);
    ASSERT_EQ(rig.timer.advance(1199.99), GameTimer::AdvanceResult::kAdvanced);
    EnemySpawn killer;
    killer.speed = 0.0;
    killer.contact_damage = 50.0;
    ASSERT_TRUE(rig.world->spawn_enemy(killer));
    EXPECT_EQ(rig.step(1.0 / 60.0).status, RunStepStatus::kLost);
    EXPECT_EQ(rig.states.state(), GameState::kLost);
    EXPECT_LT(rig.timer.elapsed_seconds(), GameTimer::kWinDurationSeconds);
}

} // namespace
