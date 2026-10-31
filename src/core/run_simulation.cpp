#include "core/run_simulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "core/combat.h"
#include "core/game_timer.h"
#include "core/player_controller.h"
#include "core/progression.h"
#include "core/run_step.h"

namespace ai_survivors {

void SimulationResult::record_sample(const SimulationSample &sample) {
    if (!samples.empty() && samples.back().time == sample.time) {
        samples.back() = sample;
    } else if (samples.size() < kMaxSamples) {
        samples.push_back(sample);
    } else {
        samples.back() = sample;
    }
}

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

// KiteBot scoring. Distances are world units; penalties and progress are in
// the same score currency, so 1 point ~ 1 unit of progress toward an orb.
constexpr double kTwoPi = 6.283185307179586;
constexpr double kSafeClearance = 48.0;   // wanted gap beyond the predicted reach
constexpr double kClearanceWeight = 1.0;  // per unit of clearance deficit
constexpr double kContactPenalty = 40.0;  // per enemy per step with clearance < 0
constexpr double kWallHitPenalty = 8.0;   // per step where the arena clamped a move
constexpr double kWallMargin = 60.0;      // preferred gap to walls at horizon end
constexpr double kWallWeight = 0.5;       // per unit of wall-margin deficit
constexpr double kProgressWeight = 1.0;   // per unit gained toward the target orb
constexpr double kCentreWeight = 0.02;    // per unit moved toward the arena centre
constexpr double kPersistWeight = 4.0;    // times cos(angle to previous heading)

// Card priorities. Higher is better; unknown or unusable cards sort last.
constexpr int kUnknownCardPriority = -1000;
constexpr int kNewWeaponPriority = 100;
constexpr int kInjuredHpPriority = 90;
constexpr int kPrimaryUpgradePriority = 70;
constexpr int kDamageOrSpeedPriority = 65;
constexpr int kOtherUpgradePriority = 60;
constexpr int kPickupPriority = 55;
constexpr int kMovePriority = 50;
constexpr int kCritPriority = 40;
constexpr int kArmorPriority = 35;
constexpr int kHealthyHpPriority = 30;
constexpr double kInjuredFraction = 0.6;

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

double dist2(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

int card_priority(const std::string &id, const ProgressionSession &progression,
                  double hp_fraction) {
    const CardDefinition *card = progression.cards().find(id);
    if (card == nullptr) {
        return kUnknownCardPriority;
    }
    const WeaponInventory &inventory = progression.inventory();
    if (card->kind == CardKind::kWeaponAcquire) {
        const WeaponDefinition *weapon =
            progression.weapons().find(card->weapon_id);
        const bool wanted = weapon != nullptr && weapon->implemented &&
                            !inventory.has(card->weapon_id) &&
                            !inventory.full();
        return wanted ? kNewWeaponPriority : kUnknownCardPriority;
    }
    if (card->kind == CardKind::kWeaponUpgrade) {
        if (!inventory.can_upgrade(card->weapon_id)) {
            return kUnknownCardPriority;
        }
        const bool primary = !inventory.entries().empty() &&
                             inventory.entries().front().id == card->weapon_id;
        return primary ? kPrimaryUpgradePriority : kOtherUpgradePriority;
    }
    if (card->kind == CardKind::kAttribute) {
        switch (card->stat) {
        case StatKind::kMaxHp:
            return hp_fraction < kInjuredFraction ? kInjuredHpPriority
                                                  : kHealthyHpPriority;
        case StatKind::kAttackSpeed:
        case StatKind::kDamage:
            return kDamageOrSpeedPriority;
        case StatKind::kPickupRadius:
            return kPickupPriority;
        case StatKind::kMoveSpeed:
            return kMovePriority;
        case StatKind::kCrit:
            return kCritPriority;
        case StatKind::kArmor:
            return kArmorPriority;
        case StatKind::kNone:
            return 0;
        }
    }
    return kUnknownCardPriority;
}

double total_xp_gained(const ProgressionSession &progression) {
    double xp = progression.levels().xp();
    for (int level = 1; level < progression.levels().level(); ++level) {
        xp += LevelUpSystem::threshold_for(level);
    }
    return xp;
}

bool is_max_hp_card(const std::string &id, const ProgressionSession &progression) {
    const CardDefinition *card = progression.cards().find(id);
    return card != nullptr && card->kind == CardKind::kAttribute &&
           card->stat == StatKind::kMaxHp;
}

SimulationSample take_sample(const GameTimer &timer, const CombatWorld &world,
                             const ProgressionSession &progression,
                             int cards_picked) {
    SimulationSample s;
    s.time = timer.elapsed_seconds();
    s.hp = world.hp();
    s.max_hp = world.max_hp();
    s.armor = progression.stats().armor;
    s.level = progression.levels().level();
    s.cards = cards_picked;
    s.kills = world.kill_count();
    s.xp_gained = total_xp_gained(progression);
    s.passive_progress = progression.passive().progress();
    s.passive_procs = progression.passive().proc_count();
    s.health = world.health_metrics();
    return s;
}

KiteBot::Input choose_input(SimulationPolicy policy, KiteBot &bot,
                            const PlayerController &player,
                            const CombatWorld &world,
                            const ProgressionSession &progression) {
    if (policy == SimulationPolicy::kStationary) {
        return KiteBot::Input{};
    }
    return bot.next_input(player.position(), world, progression);
}

} // namespace

KiteBot::Input KiteBot::next_input(Vec2 player, const CombatWorld &world,
                                   const ProgressionSession &progression) {
    if (frames_left_ <= 0) {
        cached_ = plan(player, world, progression);
        heading_ = cached_;
        frames_left_ = kDecisionFrames;
    }
    --frames_left_;
    return cached_;
}

KiteBot::Input KiteBot::plan(Vec2 p, const CombatWorld &world,
                             const ProgressionSession &progression) const {
    const CombatConfig &cfg = world.config();
    const double dt = SimulationResult::kStepSeconds;
    const double speed = progression.stats().move_speed;
    const double pr = cfg.player_radius;
    // Same reachable area as PlayerController: the body stays inside the arena.
    const double wall_x = cfg.arena_half_width - pr;
    const double wall_y = cfg.arena_half_height - pr;

    // Nearest threats first; equal distances fall back to the lower id.
    std::vector<const Enemy *> threats;
    threats.reserve(world.enemies().size());
    for (const Enemy &e : world.enemies()) {
        threats.push_back(&e);
    }
    const std::size_t keep = std::min(threats.size(), kMaxThreats);
    std::partial_sort(threats.begin(),
                      threats.begin() + static_cast<std::ptrdiff_t>(keep),
                      threats.end(),
                      [p](const Enemy *a, const Enemy *b) {
                          const double da = dist2(p, a->position);
                          const double db = dist2(p, b->position);
                          if (da != db) {
                              return da < db;
                          }
                          return a->id < b->id;
                      });
    threats.resize(keep);

    // Nearest orb within a bounded scan; first index wins on equal distance.
    const std::vector<XpOrb> &orbs = progression.experience().orbs();
    bool has_target = false;
    Vec2 target;
    double target_d2 = 0.0;
    for (std::size_t i = 0; i < orbs.size() && i < kMaxOrbScan; ++i) {
        const double d2 = dist2(p, orbs[i].position);
        if (!has_target || d2 < target_d2) {
            has_target = true;
            target_d2 = d2;
            target = orbs[i].position;
        }
    }
    const double target_d0 = has_target ? std::sqrt(target_d2) : 0.0;

    Input best;
    double best_score = -std::numeric_limits<double>::max();
    // Candidates 0..kHeadings-1 are headings; index kHeadings is stop (zero).
    for (int c = 0; c <= kHeadings; ++c) {
        Input dir;
        if (c < kHeadings) {
            const double angle = kTwoPi * c / kHeadings;
            dir.x = std::cos(angle);
            dir.y = std::sin(angle);
        }
        double score = 0.0;
        Vec2 q = p;
        for (int k = 1; k <= kHorizonFrames; ++k) {
            const double t = k * dt;
            const double rx = q.x + dir.x * speed * dt;
            const double ry = q.y + dir.y * speed * dt;
            const Vec2 next{std::clamp(rx, -wall_x, wall_x),
                            std::clamp(ry, -wall_y, wall_y)};
            if (next.x != rx || next.y != ry) {
                score -= kWallHitPenalty;
            }
            q = next;
            for (const Enemy *e : threats) {
                // Enemies may close in at their own speed during t.
                const double clear = std::sqrt(dist2(q, e->position)) - pr -
                                     e->radius - e->speed * t;
                if (clear < kSafeClearance) {
                    score -= kClearanceWeight * (kSafeClearance - clear);
                }
                if (clear < 0.0) {
                    score -= kContactPenalty;
                }
            }
        }
        const double slack =
            std::min(wall_x - std::fabs(q.x), wall_y - std::fabs(q.y));
        if (slack < kWallMargin) {
            score -= kWallWeight * (kWallMargin - slack);
        }
        if (has_target) {
            score += kProgressWeight * (target_d0 - std::sqrt(dist2(q, target)));
        }
        score += kCentreWeight * (std::sqrt(dist2(Vec2{}, p)) -
                                  std::sqrt(dist2(Vec2{}, q)));
        score += kPersistWeight * (dir.x * heading_.x + dir.y * heading_.y);
        if (score > best_score) {
            best_score = score;
            best = dir;
        }
    }
    return best;
}

std::vector<std::string> KiteBot::rank_cards(const UpgradeOffer &offer,
                                             const ProgressionSession &progression,
                                             double hp_fraction) {
    std::vector<std::pair<int, std::string>> ranked;
    ranked.reserve(offer.card_ids.size());
    for (const std::string &id : offer.card_ids) {
        ranked.emplace_back(card_priority(id, progression, hp_fraction), id);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const std::pair<int, std::string> &a,
                        const std::pair<int, std::string> &b) {
                         return a.first > b.first;
                     });
    std::vector<std::string> ids;
    ids.reserve(ranked.size());
    for (auto &entry : ranked) {
        ids.push_back(std::move(entry.second));
    }
    return ids;
}

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
    KiteBot bot;

    r.samples.reserve(SimulationResult::kMaxSamples);
    r.record_sample(take_sample(timer, world, progression, r.cards_picked));
    double next_sample_at = SimulationResult::kSampleIntervalSeconds;

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
            // Stationary keeps offer order (the weak baseline); kite ranks cards.
            const double hp_fraction =
                world.max_hp() > 0.0 ? world.hp() / world.max_hp() : 1.0;
            const std::vector<std::string> order =
                policy == SimulationPolicy::kKiteAndCollect
                    ? KiteBot::rank_cards(offer, progression, hp_fraction)
                    : offer.card_ids;
            bool offered_hp = false;
            for (const std::string &id : offer.card_ids) {
                offered_hp = offered_hp || is_max_hp_card(id, progression);
            }
            if (offered_hp) {
                ++r.hp_offers_seen;
            }
            bool applied = false;
            for (const std::string &id : order) {
                const bool picked_hp = is_max_hp_card(id, progression);
                const ChooseRunResult c = choose_run_upgrade(
                    states, world, player, progression, offer.generation, id);
                if (c.result == ChooseResult::kApplied) {
                    ++r.cards_picked;
                    if (picked_hp) {
                        ++r.hp_cards_selected;
                    } else if (offered_hp && hp_fraction >= kInjuredFraction) {
                        ++r.hp_offers_declined_healthy;
                    }
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
        const KiteBot::Input in =
            choose_input(policy, bot, player, world, progression);
        const RunStepResult step =
            advance_run(states, timer, player, world, progression,
                        SimulationResult::kStepSeconds, in.x, in.y);
        if (step.status == RunStepStatus::kRejected) {
            r.error = "advance_run rejected a fixed step";
            break;
        }
        ++r.frames;
        if (timer.elapsed_seconds() >= next_sample_at) {
            if (r.samples.size() < SimulationResult::kMaxSamples - 1) {
                r.record_sample(
                    take_sample(timer, world, progression, r.cards_picked));
            }
            next_sample_at += SimulationResult::kSampleIntervalSeconds;
        }
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
    r.xp_gained = total_xp_gained(progression);
    // Terminal state wins when game time did not advance on a lethal frame.
    const SimulationSample last =
        take_sample(timer, world, progression, r.cards_picked);
    r.record_sample(last);
    mix(r.trace_hash, static_cast<std::uint64_t>(r.final_state));
    mix_fixed(r.trace_hash, r.survival_seconds);
    return r;
}

} // namespace ai_survivors
