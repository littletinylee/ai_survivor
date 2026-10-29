#include "core/run_step.h"

#include <algorithm>
#include <cmath>

namespace ai_survivors {

RunStepResult advance_run(GameStateManager &states, GameTimer &timer,
                          PlayerController &player, CombatWorld &world,
                          ProgressionSession &progression, double dt,
                          double input_x, double input_y) {
    RunStepResult out;
    if (!std::isfinite(dt) || dt < 0.0 || !std::isfinite(input_x) ||
        !std::isfinite(input_y)) {
        out.status = RunStepStatus::kRejected;
        return out;
    }
    if (states.state() != GameState::kPlaying) {
        out.status = RunStepStatus::kFrozen;
        return out;
    }
    const double step = std::min(dt, CombatWorld::kMaxAcceptedDelta);
    out.consumed = step;

    player.step(states.state(), step, input_x, input_y);
    world.tick(states, step, player.position());
    if (states.state() == GameState::kPlaying) {
        timer.advance(step);
    }
    if (states.state() == GameState::kPlaying) {
        progression.ingest_and_collect(states.state(), world.take_kill_events(),
                                       player.position());
        out.upgrade = prepare_next_upgrade(states, progression);
    }
    out.status = states.state() == GameState::kWon    ? RunStepStatus::kWon
                 : states.state() == GameState::kLost ? RunStepStatus::kLost
                                                      : RunStepStatus::kAdvanced;
    return out;
}

UpgradePrep prepare_next_upgrade(GameStateManager &states,
                                 ProgressionSession &progression) {
    if (states.state() == GameState::kPlaying) {
        if (!progression.needs_upgrade() || !states.begin_upgrade()) {
            return UpgradePrep::kNoUpgrade;
        }
    } else if (states.state() != GameState::kUpgrade) {
        return UpgradePrep::kNoUpgrade;
    } else if (progression.cards().has_active_offer()) {
        return UpgradePrep::kOfferActive;
    }
    if (progression.advance_offer()) {
        return UpgradePrep::kOfferActive;
    }
    states.complete_upgrade();
    return UpgradePrep::kResolvedEmpty;
}

void sync_runtime_stats(CombatWorld &world, PlayerController &player,
                        const ProgressionSession &progression) {
    const PlayerRuntimeStats &s = progression.stats();
    world.update_player_stats(s.combat(), progression.weapon_damage_scale());
    player.set_move_speed(s.move_speed);
}

ChooseRunResult choose_run_upgrade(GameStateManager &states,
                                   CombatWorld &world, PlayerController &player,
                                   ProgressionSession &progression,
                                   std::uint64_t generation,
                                   const std::string &card_id) {
    ChooseRunResult out;
    if (states.state() != GameState::kUpgrade ||
        !progression.cards().has_active_offer()) {
        return out;
    }
    out.result = progression.choose(generation, card_id);
    if (out.result != ChooseResult::kApplied) {
        return out;
    }
    sync_runtime_stats(world, player, progression);
    out.next = prepare_next_upgrade(states, progression);
    if (out.next == UpgradePrep::kNoUpgrade &&
        states.state() == GameState::kUpgrade) {
        states.complete_upgrade(); // no pending levels left
        out.next = UpgradePrep::kResolvedEmpty;
    }
    return out;
}

} // namespace ai_survivors
