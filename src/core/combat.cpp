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

namespace {

constexpr std::size_t kMaxLoadout = WeaponInventory::kMaxWeapons;
constexpr double kSpreadStep = 0.2; // radians between fanned projectiles

double interval_of(const WeaponSpec &spec, const CombatStats &stats) {
    return spec.cooldown /
           sanitize(stats.attack_speed_multiplier, 0.1, 5.0, 1.0);
}

WeaponSpec legacy_spec(const CombatConfig &c) {
    WeaponSpec s;
    s.id = "bubble_shot";
    s.kind = AttackKind::kProjectile;
    s.damage = c.weapon_base_damage;
    s.cooldown = c.weapon_base_interval;
    s.range = c.weapon_range;
    s.projectile_speed = c.projectile_speed;
    s.projectile_radius = c.projectile_radius;
    s.projectile_lifetime = c.projectile_lifetime;
    return s;
}

// Rebuilds the slot list. Slots with a matching id keep their cooldown
// fraction; new ids start ready.
std::vector<WeaponManager::Slot> merge_slots(
    const std::vector<WeaponManager::Slot> &old,
    const std::vector<WeaponSpec> &specs, const CombatStats &stats) {
    std::vector<WeaponManager::Slot> next;
    next.reserve(specs.size());
    for (const WeaponSpec &spec : specs) {
        WeaponManager::Slot slot;
        slot.spec = spec;
        slot.interval = interval_of(spec, stats);
        for (const WeaponManager::Slot &o : old) {
            if (o.spec.id == spec.id) {
                const double fraction =
                    o.interval > 0.0 ? std::min(1.0, o.cooldown / o.interval)
                                     : 0.0;
                slot.cooldown = fraction * slot.interval;
                break;
            }
        }
        next.push_back(std::move(slot));
    }
    return next;
}

} // namespace

void WeaponManager::configure(const CombatStats &stats,
                              const CombatConfig &config) {
    stats_ = stats;
    slots_ = merge_slots({}, {legacy_spec(config)}, stats_);
}

void WeaponManager::reset() {
    for (Slot &s : slots_) {
        s.cooldown = 0.0;
    }
}

void WeaponManager::reconfigure(const CombatStats &stats,
                                const CombatConfig &config) {
    stats_ = stats;
    slots_ = merge_slots(slots_, {legacy_spec(config)}, stats_);
}

bool WeaponManager::set_loadout(const CombatStats &stats,
                                const std::vector<WeaponSpec> &specs) {
    if (specs.empty() || specs.size() > kMaxLoadout) {
        return false;
    }
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (!validate_weapon_spec(specs[i]).empty()) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (specs[j].id == specs[i].id) {
                return false;
            }
        }
    }
    stats_ = stats;
    slots_ = merge_slots(slots_, specs, stats_);
    return true;
}

void WeaponManager::update_stats(const CombatStats &stats) {
    stats_ = stats;
    std::vector<WeaponSpec> specs;
    specs.reserve(slots_.size());
    for (const Slot &s : slots_) {
        specs.push_back(s.spec);
    }
    slots_ = merge_slots(slots_, specs, stats_);
}

int WeaponManager::step_all(double dt, Vec2 origin, const EnemyManager &enemies,
                            ProjectileSystem &projectiles, CombatRng &rng,
                            AreaSink *area) {
    int fired = 0;
    for (Slot &slot : slots_) {
        slot.cooldown = std::max(0.0, slot.cooldown - dt);
        if (slot.cooldown > 0.0) {
            continue;
        }
        if (fire(slot, origin, enemies, projectiles, rng, area)) {
            slot.cooldown = slot.interval; // only a real attack spends it
            ++fired;
        }
    }
    return fired;
}

bool WeaponManager::fire(Slot &slot, Vec2 origin, const EnemyManager &enemies,
                         ProjectileSystem &projectiles, CombatRng &rng,
                         AreaSink *area) {
    const WeaponSpec &spec = slot.spec;
    switch (spec.kind) {
    case AttackKind::kBurst: {
        if (area == nullptr) {
            return false;
        }
        bool any = false;
        for (const Enemy &e : enemies.enemies()) {
            const double reach = spec.area_radius + e.radius;
            if (e.hp > 0.0 && dist_sq(origin, e.position) <= reach * reach) {
                any = true;
                break;
            }
        }
        if (!any) {
            return false; // stays ready; an empty sweep is not spent
        }
        const bool crit = DamageSystem::roll_crit(stats_.crit_chance, rng);
        BurstAttack b;
        b.center = origin;
        b.radius = spec.area_radius;
        b.damage = DamageSystem::outgoing_damage(spec.damage,
                                                 stats_.damage_multiplier, crit);
        b.knockback = spec.knockback;
        b.critical = crit;
        area->bursts.push_back(b);
        return true;
    }
    case AttackKind::kZone: {
        if (area == nullptr || area->zones == nullptr ||
            area->zones->size() >= AreaSink::kMaxZones) {
            return false;
        }
        const int target = enemies.nearest(origin, spec.range);
        if (target < 0) {
            return false;
        }
        const bool crit = DamageSystem::roll_crit(stats_.crit_chance, rng);
        Zone z;
        z.position = enemies.enemies()[static_cast<std::size_t>(target)].position;
        z.radius = spec.area_radius;
        z.damage = DamageSystem::outgoing_damage(spec.damage,
                                                 stats_.damage_multiplier, crit);
        z.knockback = spec.knockback;
        z.tick_interval = spec.tick_interval;
        z.next_tick = spec.tick_interval;
        z.lifetime = spec.area_lifetime;
        z.critical = crit;
        area->zones->push_back(z);
        return true;
    }
    case AttackKind::kProjectile:
        break;
    }
    const int target = enemies.nearest(origin, spec.range);
    if (target < 0) {
        return false; // stays ready; no target means no cooldown spent
    }
    if (projectiles.projectiles().size() + static_cast<std::size_t>(
                                               std::max(1, spec.projectile_count)) >
        ProjectileSystem::kMaxProjectiles) {
        return false;
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
    const double base_angle = std::atan2(dy, dx);
    const int count = std::max(1, spec.projectile_count);
    for (int i = 0; i < count; ++i) {
        const double a =
            base_angle + (static_cast<double>(i) - 0.5 * (count - 1)) * kSpreadStep;
        const bool crit = DamageSystem::roll_crit(stats_.crit_chance, rng);
        Projectile p;
        p.position = origin;
        p.velocity = Vec2{std::cos(a) * spec.projectile_speed,
                          std::sin(a) * spec.projectile_speed};
        p.damage = DamageSystem::outgoing_damage(spec.damage,
                                                 stats_.damage_multiplier, crit);
        p.radius = spec.projectile_radius;
        p.remaining_life = spec.projectile_lifetime;
        p.critical = crit;
        p.pierce_left = spec.pierce_count;
        p.knockback = spec.knockback;
        projectiles.spawn(p);
    }
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
    if (legacy_loadout_) {
        weapon_.reconfigure(stats_, config_);
    } else {
        weapon_.update_stats(stats_); // loadout damage comes from set_loadout
    }
    return true;
}

bool CombatWorld::set_loadout(const std::vector<WeaponSpec> &specs) {
    if (!weapon_.set_loadout(stats_, specs)) {
        return false;
    }
    legacy_loadout_ = false;
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
    AreaSink area;
    area.zones = &zones_;
    weapon_.step_all(h, player, enemies_, projectiles_, rng_, &area);
    // Order: burst, projectiles, zone ticks. Each phase skips already-dead
    // enemies, so a simultaneous kill is counted once.
    resolve_bursts(area.bursts);
    projectiles_.step(h);
    resolve_projectiles();
    step_zones(h);
    step_visuals(h);
    if (resolve_contacts(h, player)) {
        died = true;
        states.lose();
    }
}

bool CombatWorld::damage_enemy(Enemy &e, double damage, Vec2 source,
                               double knockback) {
    if (e.hp <= 0.0) {
        return false;
    }
    e.hp -= damage;
    if (e.hp <= 0.0) {
        ++kill_count_;
        if (kill_events_.size() < kMaxKillEvents) {
            kill_events_.push_back(KillEvent{e.position, e.xp_value, e.id});
        } else {
            ++dropped_kill_events_;
        }
    } else if (knockback > 0.0) {
        double dx = e.position.x - source.x;
        double dy = e.position.y - source.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > 1e-9) {
            dx /= d;
            dy /= d;
        } else {
            dx = 1.0;
            dy = 0.0;
        }
        e.position.x = std::min(config_.arena_half_width,
                                std::max(-config_.arena_half_width,
                                         e.position.x + dx * knockback));
        e.position.y = std::min(config_.arena_half_height,
                                std::max(-config_.arena_half_height,
                                         e.position.y + dy * knockback));
    }
    return true;
}

void CombatWorld::purge_dead() {
    std::vector<Enemy> &foes = enemies_.mutable_enemies();
    foes.erase(std::remove_if(foes.begin(), foes.end(),
                              [](const Enemy &e) { return e.hp <= 0.0; }),
               foes.end());
}

void CombatWorld::resolve_projectiles() {
    std::vector<Projectile> &shots = projectiles_.mutable_projectiles();
    std::vector<Enemy> &foes = enemies_.mutable_enemies();
    for (Projectile &p : shots) {
        for (Enemy &e : foes) {
            if (e.hp <= 0.0 || p.remaining_life <= 0.0) {
                continue;
            }
            if (std::find(p.hit_ids.begin(), p.hit_ids.end(), e.id) !=
                p.hit_ids.end()) {
                continue; // never hit the same enemy twice
            }
            const double reach = p.radius + e.radius;
            if (dist_sq(p.position, e.position) > reach * reach) {
                continue;
            }
            damage_enemy(e, p.damage, p.position, p.knockback);
            p.hit_ids.push_back(e.id);
            if (p.pierce_left <= 0) {
                p.remaining_life = 0.0; // retired on its last allowed hit
                break;
            }
            --p.pierce_left;
        }
    }
    shots.erase(std::remove_if(shots.begin(), shots.end(),
                               [](const Projectile &p) {
                                   return p.remaining_life <= 0.0;
                               }),
                shots.end());
    purge_dead();
}

void CombatWorld::resolve_bursts(const std::vector<BurstAttack> &attacks) {
    for (const BurstAttack &b : attacks) {
        for (Enemy &e : enemies_.mutable_enemies()) {
            const double reach = b.radius + e.radius;
            if (e.hp > 0.0 && dist_sq(b.center, e.position) <= reach * reach) {
                damage_enemy(e, b.damage, b.center, b.knockback);
            }
        }
        if (bursts_.size() < kMaxBurstVisuals) {
            bursts_.push_back(BurstVisual{b.center, b.radius, 0.0});
        }
    }
    purge_dead();
}

void CombatWorld::step_zones(double h) {
    for (Zone &z : zones_) {
        z.age += h;
        // Ticks land at k * tick_interval for every k with that time <= both
        // the current age and the lifetime.
        while (z.next_tick <= z.age + 1e-9 && z.next_tick <= z.lifetime + 1e-9) {
            for (Enemy &e : enemies_.mutable_enemies()) {
                const double reach = z.radius + e.radius;
                if (e.hp > 0.0 && dist_sq(z.position, e.position) <= reach * reach) {
                    damage_enemy(e, z.damage, z.position, z.knockback);
                }
            }
            z.next_tick += z.tick_interval;
        }
    }
    zones_.erase(std::remove_if(zones_.begin(), zones_.end(),
                                [](const Zone &z) {
                                    return z.age >= z.lifetime - 1e-9;
                                }),
                 zones_.end());
    purge_dead();
}

void CombatWorld::step_visuals(double h) {
    for (BurstVisual &v : bursts_) {
        v.age += h;
    }
    bursts_.erase(std::remove_if(bursts_.begin(), bursts_.end(),
                                 [](const BurstVisual &v) {
                                     return v.age >= BurstVisual::kDuration;
                                 }),
                  bursts_.end());
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
