#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/character.h"
#include "core/game_state.h"

namespace ai_survivors {

enum class SimulationPolicy {
    kStationary,   // never moves
    kKiteAndCollect, // flees nearby enemies, otherwise walks to the nearest orb
};

// Outcome of one deterministic headless run. Nothing here is forced: the
// terminal state is whatever advance_run produced.
struct SimulationResult {
    static constexpr std::uint64_t kMaxIterations = 100000;
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

    std::size_t peak_enemies = 0;
    std::size_t peak_projectiles = 0;
    std::size_t peak_orbs = 0;
    std::size_t peak_weapons = 0;

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
