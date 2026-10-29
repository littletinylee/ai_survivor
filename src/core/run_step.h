#pragma once

#include <cstdint>
#include <string>

#include "core/combat.h"
#include "core/game_state.h"
#include "core/game_timer.h"
#include "core/player_controller.h"
#include "core/progression.h"

namespace ai_survivors {

// Shared real-gameplay orchestration. The native scene and the headless
// simulation both call exactly these helpers; neither re-implements the frame
// order or the card-choice flow.

enum class UpgradePrep {
    kNoUpgrade,   // nothing pending (or wrong state); state unchanged
    kOfferActive, // state is Upgrade and an offer is waiting for a choice
    kResolvedEmpty, // pending levels had an empty pool; Upgrade was completed
};

enum class RunStepStatus {
    kRejected, // dt/input non-finite or dt negative; nothing changed
    kFrozen,   // state is not Playing; nothing changed
    kAdvanced, // one clamped delta was consumed and the run is still going
    kWon,
    kLost,
};

struct RunStepResult {
    RunStepStatus status = RunStepStatus::kFrozen;
    UpgradePrep upgrade = UpgradePrep::kNoUpgrade;
    double consumed = 0.0; // the single delta used by movement, combat, timer
};

// One Playing frame. dt is clamped ONCE to CombatWorld::kMaxAcceptedDelta and
// that same value drives movement, combat and the survival timer. Order:
// movement, combat, timer (only if still Playing), XP kills/pickup (only if
// still Playing), then the upgrade offer. Death in the same frame as the 1200 s
// boundary wins because combat runs before the timer.
RunStepResult advance_run(GameStateManager &states, GameTimer &timer,
                          PlayerController &player, CombatWorld &world,
                          ProgressionSession &progression, double dt,
                          double input_x, double input_y);

// Playing with pending levels -> Upgrade, then the next offer. Never
// regenerates an offer that is already active. An empty pool resolves pending
// levels without reward and completes Upgrade.
UpgradePrep prepare_next_upgrade(GameStateManager &states,
                                 ProgressionSession &progression);

// Copies progression stats into the combat world and player speed.
void sync_runtime_stats(CombatWorld &world, PlayerController &player,
                        const ProgressionSession &progression);

// Installs the actual selected initial weapon(s) into a fresh world. Empty
// string on success; otherwise the run must not start.
std::string initialize_run_loadout(CombatWorld &world,
                                   const ProgressionSession &progression);

struct ChooseRunResult {
    ChooseResult result = ChooseResult::kNoOffer;
    UpgradePrep next = UpgradePrep::kNoUpgrade; // only meaningful if applied
};

// Validates Upgrade state + active offer, applies the card through
// ProgressionSession::choose, syncs world/player, then opens the next queued
// offer or completes Upgrade. Rejections mutate nothing.
ChooseRunResult choose_run_upgrade(GameStateManager &states,
                                   CombatWorld &world, PlayerController &player,
                                   ProgressionSession &progression,
                                   std::uint64_t generation,
                                   const std::string &card_id);

} // namespace ai_survivors
