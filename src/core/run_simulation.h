#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/character.h"
#include "core/combat.h"
#include "core/game_state.h"
#include "core/player_controller.h"
#include "core/progression.h"

namespace ai_survivors {

enum class SimulationPolicy {
    kStationary,   // never moves; takes the first offered card
    kKiteAndCollect, // KiteBot steering; takes the highest-ranked offered card
};

// Deterministic lookahead steering for kKiteAndCollect. It only reads the world
// and emits an ordinary normalized input; it never mutates game state. Public so
// scenario tests can drive single decisions.
//
// The bot scores kHeadings directions plus stop by simulating kHorizonFrames
// frames against the nearest kMaxThreats enemies (treated as stationary but
// with their speed added as a clearance margin) and clamps the player to the
// arena. Results are cached for kDecisionFrames frames, which is 10 Hz at 60 Hz.
class KiteBot {
public:
    static constexpr int kHeadings = 16;            // candidate directions, plus stop
    static constexpr int kHorizonFrames = 10;       // lookahead, 1/6 s
    static constexpr int kDecisionFrames = 6;       // re-plan every 0.1 s
    static constexpr std::size_t kMaxThreats = 32;  // nearest enemies scored
    static constexpr std::size_t kMaxOrbScan = 128; // orbs scanned for a target

    struct Input {
        double x = 0.0;
        double y = 0.0;
    };

    // Returns the cached decision, re-planning once every kDecisionFrames calls.
    Input next_input(Vec2 player, const CombatWorld &world,
                     const ProgressionSession &progression);

    // Offered card ids sorted by the bot's priority (highest first). Ties keep
    // offer order. Unknown ids sort last but are still returned, so the caller's
    // fallback loop still tries them. hp_fraction is HP / max HP.
    static std::vector<std::string> rank_cards(const UpgradeOffer &offer,
                                               const ProgressionSession &progression,
                                               double hp_fraction);

private:
    Input plan(Vec2 player, const CombatWorld &world,
               const ProgressionSession &progression) const;

    Input cached_;
    Input heading_; // last planned direction, used for persistence
    int frames_left_ = 0;
};

// One diagnostic snapshot of a run. Read-only observation: taking a sample uses
// no RNG and mutates nothing, and samples never feed trace_hash.
struct SimulationSample {
    double time = 0.0;          // GameTimer seconds (equals combat time while Playing)
    double hp = 0.0;
    double max_hp = 0.0;
    double armor = 0.0;
    int level = 1;
    int cards = 0;              // cards picked so far
    std::uint32_t kills = 0;
    double xp_gained = 0.0;     // total XP banked so far, incl. spent levels
    double passive_progress = 0.0;
    std::uint32_t passive_procs = 0;
    HealthMetrics health;       // cumulative; by-kind arrays follow EnemyKind
};

// Outcome of one deterministic headless run. Nothing here is forced: the
// terminal state is whatever advance_run produced.
struct SimulationResult {
    static constexpr std::uint64_t kMaxIterations = 100000;
    // Timeline: start, one per kSampleIntervalSeconds, and the terminal sample.
    static constexpr double kSampleIntervalSeconds = 60.0;
    static constexpr std::size_t kMaxSamples = 22;
    static constexpr double kStepSeconds = 1.0 / 60.0;

    std::string error; // non-empty: the run never started
    GameState final_state = GameState::kMenu;
    bool terminated = false;           // Won or Lost
    bool hit_iteration_limit = false;  // bound reached without terminal state
    bool stalled = false;              // an offer had no applicable card

    std::uint64_t frames = 0;          // Playing frames advanced
    double survival_seconds = 0.0;     // GameTimer clock
    double final_hp = 0.0;
    double max_hp = 0.0;
    std::uint32_t kills = 0;
    int final_level = 1;
    double xp_gained = 0.0;            // total XP banked, incl. spent levels

    int upgrade_offers = 0;
    int cards_picked = 0;
    int invalid_selections = 0;
    int peak_pending_levels = 0;

    // Derived only from real offers and accepted choices. "Healthy" means HP
    // fraction >= the bot's injured threshold at the time of the offer.
    int hp_offers_seen = 0;             // offers containing a max-HP card
    int hp_cards_selected = 0;          // accepted max-HP cards
    int hp_offers_declined_healthy = 0; // HP offered, other card taken, healthy

    std::vector<SimulationSample> samples; // at most kMaxSamples

    // Called in nondecreasing game-clock order. A lethal frame does not advance
    // that clock: replace a sample at the same time with the terminal state.
    // Retain the latest sample even if the diagnostic capacity is reached.
    void record_sample(const SimulationSample &sample);

    std::size_t peak_enemies = 0;
    std::size_t peak_projectiles = 0;
    std::size_t peak_orbs = 0;
    std::size_t peak_weapons = 0;
    std::size_t peak_zones = 0;        // active data_pool-style zones; cap 32

    int frozen_violations = 0;   // clocks moved during Upgrade
    int terminal_violations = 0; // clocks/state moved after a terminal state

    std::uint64_t trace_hash = 0; // FNV-1a over fixed-point per-frame samples
};

// Runs a full headless run with 1/60 s steps through advance_run and
// choose_run_upgrade (the same helpers as the native scene). Upgrade choices
// do not advance survival time; they are made automatically.
SimulationResult run_simulation(const CharacterDefinition &character,
                                std::uint64_t seed, SimulationPolicy policy);

} // namespace ai_survivors
