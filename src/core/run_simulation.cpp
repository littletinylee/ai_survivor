#include "core/run_simulation.h"

#include <algorithm>
#include <cmath>

#include "core/combat.h"
#include "core/game_timer.h"
#include "core/player_controller.h"
#include "core/progression.h"
#include "core/run_step.h"

namespace ai_survivors {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void mix(std::uint64_t &h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= kFnvPrime;
    }
}

// Fixed-point: round to 1/1000 so the hash is explicit integer arithmetic.
void mix_fixed(std::uint64_t &h, double v) {
    mix(h, static_cast<std::uint64_t>(
               static_cast<std::int64_t>(std::llround(v * 1000.0))));
}

void mix_text(std::uint64_t &h, const std::string &s) {
    for (const char c : s) {
        mix(h, static_cast<unsigned char>(c));
    }
}

struct Input {
    double x = 0.0;
    double y = 0.0;
};

Input choose_input(SimulationPolicy policy, const PlayerController &player,
                   const CombatWorld &world,
                   const ProgressionSession &progression) {
    Input in;
    if (policy == SimulationPolicy::kStationary) {
        return in;
    }
    const Vec2 p = player.position();
    constexpr double kFleeRange = 220.0;
    double best = kFleeRange * kFleeRange;
    const Enemy *nearest = nullptr;
    for (const Enemy &e : world.enemies()) {
        const double dx = e.position.x - p.x;
        const double dy = e.position.y - p.y;
        const double d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            nearest = &e;
        }
    }
    const CombatConfig &cfg = world.config();
    if (nearest != nullptr) {
        in.x = p.x - nearest->position.x;
        in.y = p.y - nearest->position.y;
        // Pull toward the centre near walls so fleeing does not pin the bot.
        if (std::fabs(p.x) > cfg.arena_half_width - 120.0) in.x -= p.x * 2.0;
        if (std::fabs(p.y) > cfg.arena_half_height - 90.0) in.y -= p.y * 2.0;
    } else {
        double best_orb = 1e18;
        for (const XpOrb &o : progression.experience().orbs()) {
            const double dx = o.position.x - p.x;
            const double dy = o.position.y - p.y;
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_orb) {
                best_orb = d2;
                in.x = dx;
                in.y = dy;
            }
        }
    }
    const double len = std::sqrt(in.x * in.x + in.y * in.y);
    if (len > 1e-9) {
        in.x /= len;
        in.y /= len;
    } else {
        in = Input{};
    }
    return in;
}

} // namespace

SimulationResult run_simulation(const CharacterDefinition &character,
                                std::uint64_t seed, SimulationPolicy policy) {
    SimulationResult r;
    r.trace_hash = kFnvOffset;
    r.error = validate_character(character);
    if (!r.error.empty()) {
        return r;
    }
    GameStateManager states;
    GameTimer timer(states);
    const WeaponRegistry weapons = WeaponRegistry::make_default();
    ProgressionSession progression(character, weapons, seed ^ 0x51D00000ULL);
    if (!progression.init_error().empty()) {
        r.error = progression.init_error();
        return r;
    }
    const CombatConfig config;
    PlayerController player(config.arena_half_width, config.arena_half_height,
                            config.player_radius,
                            progression.stats().move_speed);
    CombatWorld world(CombatStats::from_character(character),
                      seed ^ 0xA15D0000ULL, config);
    r.error = initialize_run_loadout(world, progression);
    if (!r.error.empty()) {
        return r;
    }
    states.start_run();

    // Total passes (frames and upgrade choices) are bounded by kMaxIterations.
    for (std::uint64_t iter = 0;
         iter < SimulationResult::kMaxIterations && !states.is_terminal();
         ++iter) {
        if (states.state() == GameState::kUpgrade) {
            // Survival time must stay frozen while a choice is pending.
            const double t0 = timer.elapsed_seconds();
            const double c0 = world.combat_time();
            advance_run(states, timer, player, world, progression,
                        SimulationResult::kStepSeconds, 0.0, 0.0);
            if (timer.elapsed_seconds() != t0 || world.combat_time() != c0) {
                ++r.frozen_violations;
            }
            const UpgradeOffer offer = progression.cards().current_offer();
            ++r.upgrade_offers;
            bool applied = false;
            for (const std::string &id : offer.card_ids) {
                const ChooseRunResult c = choose_run_upgrade(
                    states, world, player, progression, offer.generation, id);
                if (c.result == ChooseResult::kApplied) {
                    ++r.cards_picked;
                    mix_text(r.trace_hash, id);
                    applied = true;
                    break;
                }
                ++r.invalid_selections;
            }
            if (!applied) {
                r.stalled = true;
                break;
            }
            r.peak_weapons =
                std::max(r.peak_weapons, progression.inventory().entries().size());
            continue; // choices consume no survival time
        }
        const Input in = choose_input(policy, player, world, progression);
        const RunStepResult step =
            advance_run(states, timer, player, world, progression,
                        SimulationResult::kStepSeconds, in.x, in.y);
        if (step.status == RunStepStatus::kRejected) {
            r.error = "advance_run rejected a fixed step";
            break;
        }
        ++r.frames;
        r.peak_enemies = std::max(r.peak_enemies, world.enemies().size());
        r.peak_projectiles =
            std::max(r.peak_projectiles, world.projectiles().size());
        r.peak_orbs =
            std::max(r.peak_orbs, progression.experience().orbs().size());
        r.peak_zones = std::max(r.peak_zones, world.zones().size());
        r.peak_weapons =
            std::max(r.peak_weapons, progression.inventory().entries().size());
        r.peak_pending_levels =
            std::max(r.peak_pending_levels, progression.levels().pending_levels());

        mix_fixed(r.trace_hash, player.position().x);
        mix_fixed(r.trace_hash, player.position().y);
        mix_fixed(r.trace_hash, world.hp());
        mix(r.trace_hash, world.kill_count());
        mix(r.trace_hash, world.enemies().size());
        mix(r.trace_hash, world.projectiles().size());
        mix(r.trace_hash, world.zones().size());
        mix(r.trace_hash, progression.experience().orbs().size());
        mix(r.trace_hash, static_cast<std::uint64_t>(progression.levels().level()));
        mix_fixed(r.trace_hash, progression.levels().xp());
        mix(r.trace_hash, static_cast<std::uint64_t>(states.state()));
    }

    // Terminal clocks must not move.
    if (states.is_terminal()) {
        const GameState s0 = states.state();
        const double t0 = timer.elapsed_seconds();
        const double c0 = world.combat_time();
        advance_run(states, timer, player, world, progression,
                    SimulationResult::kStepSeconds, 1.0, 0.0);
        if (states.state() != s0 || timer.elapsed_seconds() != t0 ||
            world.combat_time() != c0) {
            ++r.terminal_violations;
        }
    } else if (!r.stalled && r.error.empty()) {
        r.hit_iteration_limit = true;
    }

    r.final_state = states.state();
    r.terminated = states.is_terminal();
    r.survival_seconds = timer.elapsed_seconds();
    r.final_hp = world.hp();
    r.max_hp = world.max_hp();
    r.kills = world.kill_count();
    r.final_level = progression.levels().level();
    double xp = progression.levels().xp();
    for (int level = 1; level < r.final_level; ++level) {
        xp += LevelUpSystem::threshold_for(level);
    }
    r.xp_gained = xp;
    mix(r.trace_hash, static_cast<std::uint64_t>(r.final_state));
    mix_fixed(r.trace_hash, r.survival_seconds);
    return r;
}

} // namespace ai_survivors
