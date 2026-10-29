#include "core/combat.h"

#include <algorithm>
#include <cmath>

namespace ai_survivors {

namespace {

double sanitize(double v, double lo, double hi, double fallback) {
    if (!std::isfinite(v)) {
        return fallback;
    }
    return std::min(hi, std::max(lo, v));
}

bool finite_vec(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

double dist_sq(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace

// ---------------------------------------------------------------- CombatRng

std::uint64_t CombatRng::next() {
    state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double CombatRng::next_unit() {
    return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
}

// -------------------------------------------------------------- CombatStats

CombatStats CombatStats::from_character(const CharacterDefinition &c) {
    CombatStats s;
    s.max_hp = c.max_hp;
    s.armor = c.armor;
    s.damage_multiplier = c.damage_multiplier;
    s.attack_speed_multiplier = c.attack_speed_multiplier;
    s.crit_chance = c.crit_chance;
    return s;
}

// ------------------------------------------------------------- DamageSystem

double DamageSystem::mitigate(double raw, double armor) {
    if (!std::isfinite(raw) || raw <= 0.0) {
        return 0.0;
    }
    const double a = sanitize(armor, 0.0, kMaxArmor, 0.0);
    return std::max(kMinHitDamage, raw * 100.0 / (100.0 + a));
}

bool DamageSystem::roll_crit(double chance, CombatRng &rng) {
    const double roll = rng.next_unit();
    return roll < sanitize(chance, 0.0, 1.0, 0.0);
}

double DamageSystem::outgoing_damage(double base, double multiplier,
                                     bool critical) {
    if (!std::isfinite(base) || base <= 0.0) {
        return 0.0;
    }
    const double m = sanitize(multiplier, kMinDamageMultiplier,
                              kMaxDamageMultiplier, 1.0);
    return base * m * (critical ? kCritMultiplier : 1.0);
}

// ------------------------------------------------------------- EnemyManager

bool EnemyManager::spawn(const EnemySpawn &spec, double radius) {
    if (enemies_.size() >= kMaxEnemies || !finite_vec(spec.position)) {
        return false;
    }
    Enemy e;
    e.id = next_id_++;
    e.position = spec.position;
    e.hp = e.max_hp = sanitize(spec.hp, 1.0, 1e9, 20.0);
    e.radius = radius;
    e.speed = sanitize(spec.speed, 0.0, 2000.0, 70.0);
    e.contact_damage = sanitize(spec.contact_damage, 0.0, 1e6, 8.0);
    e.xp_value = sanitize(spec.xp_value, 0.0, 1e6, 0.0);
    enemies_.push_back(e);
    return true;
}

void EnemyManager::step(double dt, Vec2 target) {
    for (Enemy &e : enemies_) {
        const double dx = target.x - e.position.x;
        const double dy = target.y - e.position.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > 1e-9) {
            const double move = std::min(e.speed * dt, d);
            e.position.x += dx / d * move;
            e.position.y += dy / d * move;
        }
    }
}

int EnemyManager::nearest(Vec2 from, double max_range) const {
    int best = -1;
    double best_d = max_range * max_range;
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        if (enemies_[i].hp <= 0.0) {
            continue;
        }
        const double d = dist_sq(from, enemies_[i].position);
        if (d <= best_d) {
            best_d = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void EnemyManager::clear() {
    enemies_.clear();
    next_id_ = 1;
}

// --------------------------------------------------------- ProjectileSystem

bool ProjectileSystem::spawn(const Projectile &projectile) {
    if (projectiles_.size() >= kMaxProjectiles) {
        return false;
    }
    projectiles_.push_back(projectile);
    return true;
}

void ProjectileSystem::step(double dt) {
    for (Projectile &p : projectiles_) {
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.remaining_life -= dt;
    }
    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(),
                       [](const Projectile &p) { return p.remaining_life <= 0.0; }),
        projectiles_.end());
}

void ProjectileSystem::clear() { projectiles_.clear(); }

// ----------------------------------------------------------- WeaponManager

void WeaponManager::configure(const CombatStats &stats,
                              const CombatConfig &config) {
    stats_ = stats;
    config_ = config;
    const double speed =
        sanitize(stats.attack_speed_multiplier, 0.1, 5.0, 1.0);
    interval_ = config.weapon_base_interval / speed;
    cooldown_ = 0.0;
}

void WeaponManager::reset() { cooldown_ = 0.0; }

void WeaponManager::reconfigure(const CombatStats &stats,
                                const CombatConfig &config) {
    const double old_interval = interval_;
    const double fraction =
        old_interval > 0.0 ? std::min(1.0, cooldown_ / old_interval) : 0.0;
    stats_ = stats;
    config_ = config;
    const double speed =
        sanitize(stats.attack_speed_multiplier, 0.1, 5.0, 1.0);
    interval_ = config.weapon_base_interval / speed;
    cooldown_ = fraction * interval_;
}

bool WeaponManager::step(double dt, Vec2 origin, const EnemyManager &enemies,
                         ProjectileSystem &projectiles, CombatRng &rng) {
    cooldown_ = std::max(0.0, cooldown_ - dt);
    if (cooldown_ > 0.0) {
        return false;
    }
    const int target = enemies.nearest(origin, config_.weapon_range);
    if (target < 0) {
        return false; // stays ready; no target means no cooldown spent
    }
    const Vec2 tp = enemies.enemies()[static_cast<std::size_t>(target)].position;
    double dx = tp.x - origin.x;
    double dy = tp.y - origin.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d > 1e-9) {
        dx /= d;
        dy /= d;
    } else {
        dx = 1.0;
        dy = 0.0;
    }
    const bool crit = DamageSystem::roll_crit(stats_.crit_chance, rng);
    Projectile p;
    p.position = origin;
    p.velocity = Vec2{dx * config_.projectile_speed, dy * config_.projectile_speed};
    p.damage = DamageSystem::outgoing_damage(config_.weapon_base_damage,
                                             stats_.damage_multiplier, crit);
    p.radius = config_.projectile_radius;
    p.remaining_life = config_.projectile_lifetime;
    p.critical = crit;
    if (!projectiles.spawn(p)) {
        return false;
    }
    cooldown_ = interval_;
    return true;
}

// ------------------------------------------------------------ WaveDirector

namespace {
constexpr double kFirstWaveInterval = 1.2;
constexpr double kMinWaveInterval = 0.3;
} // namespace

void WaveDirector::configure(const CombatConfig &config) {
    config_ = config;
    reset();
}

void WaveDirector::reset() { accumulator_ = kFirstWaveInterval; }

void WaveDirector::step(double dt, double elapsed, EnemyManager &enemies,
                        CombatRng &rng) {
    if (!config_.waves_enabled) {
        return;
    }
    const double interval =
        std::max(kMinWaveInterval, kFirstWaveInterval - elapsed * 0.00075);
    accumulator_ += dt;
    if (accumulator_ < interval) {
        return;
    }
    accumulator_ -= interval;

    const int count = 1 + static_cast<int>(elapsed / 120.0);
    const double hw = config_.arena_half_width;
    const double hh = config_.arena_half_height;
    const double w = 2.0 * hw;
    const double h = 2.0 * hh;
    for (int i = 0; i < count; ++i) {
        // Walk the arena perimeter clockwise from the top-left corner.
        double t = rng.next_unit() * 2.0 * (w + h);
        Vec2 pos;
        if (t < w) {
            pos = Vec2{-hw + t, -hh};
        } else if ((t -= w) < h) {
            pos = Vec2{hw, -hh + t};
        } else if ((t -= h) < w) {
            pos = Vec2{hw - t, hh};
        } else {
            t -= w;
            pos = Vec2{-hw, hh - t};
        }
        EnemySpawn spec;
        spec.position = pos;
        spec.hp = 20.0 + elapsed * 0.05;
        spec.speed = 70.0 + std::min(50.0, elapsed * 0.04);
        spec.contact_damage = 8.0 + elapsed * 0.005;
        spec.xp_value = 5.0;
        if (!enemies.spawn(spec, config_.enemy_radius)) {
            break;
        }
    }
}

// ------------------------------------------------------------- CombatWorld

CombatWorld::CombatWorld(const CombatStats &stats, std::uint64_t seed,
                         const CombatConfig &config)
    : stats_(stats), config_(config), rng_(seed) {
    stats_.max_hp = sanitize(stats.max_hp, 1.0, 1e6, 100.0);
    stats_.armor = sanitize(stats.armor, 0.0, DamageSystem::kMaxArmor, 0.0);
    stats_.damage_multiplier =
        sanitize(stats.damage_multiplier, DamageSystem::kMinDamageMultiplier,
                 DamageSystem::kMaxDamageMultiplier, 1.0);
    stats_.attack_speed_multiplier =
        sanitize(stats.attack_speed_multiplier, 0.1, 5.0, 1.0);
    stats_.crit_chance = sanitize(stats.crit_chance, 0.0, 1.0, 0.0);
    hp_ = stats_.max_hp;
    base_weapon_damage_ = config_.weapon_base_damage;
    weapon_.configure(stats_, config_);
    waves_.configure(config_);
    kill_events_.reserve(kMaxKillEvents);
}

bool CombatWorld::spawn_enemy(const EnemySpawn &spec) {
    return enemies_.spawn(spec, config_.enemy_radius);
}

bool CombatWorld::update_player_stats(const CombatStats &stats,
                                      double weapon_damage_scale) {
    if (!std::isfinite(stats.max_hp) || !std::isfinite(stats.armor) ||
        !std::isfinite(stats.damage_multiplier) ||
        !std::isfinite(stats.attack_speed_multiplier) ||
        !std::isfinite(stats.crit_chance) ||
        !std::isfinite(weapon_damage_scale) || weapon_damage_scale <= 0.0) {
        return false;
    }
    CombatStats next;
    next.max_hp = sanitize(stats.max_hp, 1.0, 1e6, 100.0);
    next.armor = sanitize(stats.armor, 0.0, DamageSystem::kMaxArmor, 0.0);
    next.damage_multiplier =
        sanitize(stats.damage_multiplier, DamageSystem::kMinDamageMultiplier,
                 DamageSystem::kMaxDamageMultiplier, 1.0);
    next.attack_speed_multiplier =
        sanitize(stats.attack_speed_multiplier, 0.1, 5.0, 1.0);
    next.crit_chance = sanitize(stats.crit_chance, 0.0, 1.0, 0.0);

    const double added = next.max_hp - stats_.max_hp;
    stats_ = next;
    hp_ = std::min(next.max_hp, hp_ + std::max(0.0, added));
    config_.weapon_base_damage =
        sanitize(base_weapon_damage_ * weapon_damage_scale, 0.0, 1e9,
                 base_weapon_damage_);
    weapon_.reconfigure(stats_, config_);
    return true;
}

std::vector<KillEvent> CombatWorld::take_kill_events() {
    std::vector<KillEvent> out;
    out.swap(kill_events_);
    kill_events_.reserve(kMaxKillEvents);
    return out;
}

CombatWorld::TickResult CombatWorld::tick(GameStateManager &states, double dt,
                                          Vec2 player_position) {
    if (!std::isfinite(dt) || dt < 0.0 || !finite_vec(player_position)) {
        return TickResult::kRejected;
    }
    if (states.state() != GameState::kPlaying) {
        return TickResult::kFrozen;
    }
    dt = std::min(dt, kMaxAcceptedDelta);
    if (dt <= 0.0) {
        return TickResult::kAdvanced;
    }
    const int n = std::max(1, static_cast<int>(std::ceil(dt / kSubstep - 1e-9)));
    const double h = dt / n;
    bool died = false;
    for (int i = 0; i < n && !died; ++i) {
        substep(h, player_position, states, died);
    }
    return died ? TickResult::kPlayerDied : TickResult::kAdvanced;
}

void CombatWorld::substep(double h, Vec2 player, GameStateManager &states,
                          bool &died) {
    combat_time_ += h;
    waves_.step(h, combat_time_, enemies_, rng_);
    enemies_.step(h, player);
    weapon_.step(h, player, enemies_, projectiles_, rng_);
    projectiles_.step(h);
    resolve_projectiles();
    if (resolve_contacts(h, player)) {
        died = true;
        states.lose();
    }
}

void CombatWorld::resolve_projectiles() {
    std::vector<Projectile> &shots = projectiles_.mutable_projectiles();
    std::vector<Enemy> &foes = enemies_.mutable_enemies();
    for (Projectile &p : shots) {
        for (Enemy &e : foes) {
            if (e.hp <= 0.0) {
                continue;
            }
            const double reach = p.radius + e.radius;
            if (dist_sq(p.position, e.position) > reach * reach) {
                continue;
            }
            e.hp -= p.damage;
            p.remaining_life = 0.0; // retired: one hit per projectile
            if (e.hp <= 0.0) {
                ++kill_count_;
                if (kill_events_.size() < kMaxKillEvents) {
                    kill_events_.push_back(KillEvent{e.position, e.xp_value, e.id});
                } else {
                    ++dropped_kill_events_;
                }
            }
            break;
        }
    }
    shots.erase(std::remove_if(shots.begin(), shots.end(),
                               [](const Projectile &p) {
                                   return p.remaining_life <= 0.0;
                               }),
                shots.end());
    foes.erase(std::remove_if(foes.begin(), foes.end(),
                              [](const Enemy &e) { return e.hp <= 0.0; }),
               foes.end());
}

bool CombatWorld::resolve_contacts(double h, Vec2 player) {
    invulnerable_ = std::max(0.0, invulnerable_ - h);
    for (Enemy &e : enemies_.mutable_enemies()) {
        e.contact_cooldown = std::max(0.0, e.contact_cooldown - h);
        const double reach = e.radius + config_.player_radius;
        if (e.contact_cooldown > 0.0 || invulnerable_ > 0.0 ||
            dist_sq(player, e.position) > reach * reach) {
            continue;
        }
        hp_ -= DamageSystem::mitigate(e.contact_damage, stats_.armor);
        e.contact_cooldown = config_.contact_interval;
        invulnerable_ = config_.player_invulnerability;
        if (hp_ <= 0.0) {
            hp_ = 0.0;
            return true;
        }
    }
    return false;
}

} // namespace ai_survivors
