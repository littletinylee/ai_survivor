#include "core/weapon.h"

#include <cmath>

#include "core/character.h"

namespace ai_survivors {

namespace {

constexpr double kBubbleDamagePerLevel = 0.2;

bool in_range(double v, double lo, double hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}

std::string validate_shape(AttackKind kind, double damage, double cooldown,
                           double range, int count, double speed,
                           double proj_radius, double proj_life, int pierce,
                           double knockback, double area_radius,
                           double area_life, double tick) {
    if (!in_range(damage, 0.0, WeaponLimits::kMaxDamage) || damage <= 0.0) {
        return "weapon damage out of range";
    }
    if (!in_range(cooldown, WeaponLimits::kMinCooldown,
                  WeaponLimits::kMaxCooldown)) {
        return "weapon cooldown out of range";
    }
    if (!in_range(range, 1.0, WeaponLimits::kMaxRange)) {
        return "weapon range out of range";
    }
    if (!in_range(knockback, 0.0, WeaponLimits::kMaxKnockback)) {
        return "weapon knockback out of range";
    }
    switch (kind) {
    case AttackKind::kProjectile:
        if (count < 1 || count > WeaponLimits::kMaxProjectileCount) {
            return "projectile_count out of range";
        }
        if (pierce < 0 || pierce > WeaponLimits::kMaxPierce) {
            return "pierce_count out of range";
        }
        if (!in_range(speed, 1.0, WeaponLimits::kMaxSpeed) ||
            !in_range(proj_radius, 0.5, WeaponLimits::kMaxRadius) ||
            !in_range(proj_life, 0.05, WeaponLimits::kMaxLifetime)) {
            return "projectile speed/radius/lifetime out of range";
        }
        return std::string();
    case AttackKind::kBurst:
        if (!in_range(area_radius, 1.0, WeaponLimits::kMaxRadius)) {
            return "burst area_radius out of range";
        }
        return std::string();
    case AttackKind::kZone:
        if (!in_range(area_radius, 1.0, WeaponLimits::kMaxRadius) ||
            !in_range(area_life, WeaponLimits::kMinTick,
                      WeaponLimits::kMaxLifetime) ||
            !in_range(tick, WeaponLimits::kMinTick, WeaponLimits::kMaxLifetime) ||
            tick > area_life) {
            return "zone radius/lifetime/tick_interval out of range";
        }
        return std::string();
    }
    return "unsupported attack kind";
}

WeaponSpec spec_at_level(const WeaponDefinition &w, int level) {
    WeaponSpec s;
    s.id = w.id;
    s.level = level;
    s.kind = w.attack_kind;
    s.damage = w.base_damage *
               (1.0 + w.damage_per_level * static_cast<double>(level - 1));
    s.cooldown = w.cooldown;
    s.range = w.range;
    s.projectile_count = w.projectile_count;
    s.projectile_speed = w.projectile_speed;
    s.projectile_radius = w.projectile_radius;
    s.projectile_lifetime = w.projectile_lifetime;
    s.pierce_count = w.pierce_count;
    s.knockback = w.knockback;
    s.area_radius = w.area_radius;
    s.area_lifetime = w.area_lifetime;
    s.tick_interval = w.tick_interval;
    return s;
}

} // namespace

std::string validate_weapon_definition(const WeaponDefinition &w) {
    return validate_shape(w.attack_kind, w.base_damage, w.cooldown, w.range,
                          w.projectile_count, w.projectile_speed,
                          w.projectile_radius, w.projectile_lifetime,
                          w.pierce_count, w.knockback, w.area_radius,
                          w.area_lifetime, w.tick_interval);
}

std::string validate_weapon_spec(const WeaponSpec &s) {
    if (!is_valid_identifier(s.id)) {
        return "invalid weapon id";
    }
    if (s.level < 1 || s.level > WeaponRegistry::kMaxWeaponLevel) {
        return "weapon level out of range";
    }
    return validate_shape(s.kind, s.damage, s.cooldown, s.range,
                          s.projectile_count, s.projectile_speed,
                          s.projectile_radius, s.projectile_lifetime,
                          s.pierce_count, s.knockback, s.area_radius,
                          s.area_lifetime, s.tick_interval);
}

std::string compile_loadout(const WeaponRegistry &registry,
                            const WeaponInventory &inventory,
                            std::vector<WeaponSpec> &out) {
    std::vector<WeaponSpec> specs;
    if (inventory.entries().empty()) {
        return "empty loadout";
    }
    for (const WeaponInventory::Entry &e : inventory.entries()) {
        const WeaponDefinition *w = registry.find(e.id);
        if (w == nullptr || !w->implemented) {
            return "loadout weapon unavailable: " + e.id;
        }
        if (e.level < 1 || e.level > w->max_level) {
            return "loadout level out of range: " + e.id;
        }
        specs.push_back(spec_at_level(*w, e.level));
        const std::string err = validate_weapon_spec(specs.back());
        if (!err.empty()) {
            return err + ": " + e.id;
        }
    }
    out = std::move(specs);
    return std::string();
}

// ---------------------------------------------------------- WeaponRegistry

std::string WeaponRegistry::add(const WeaponDefinition &w) {
    if (!is_valid_identifier(w.id)) {
        return "invalid weapon id";
    }
    if (w.name.empty()) {
        return "empty weapon name";
    }
    if (w.max_level < 1 || w.max_level > kMaxWeaponLevel) {
        return "weapon max_level out of range";
    }
    if (!std::isfinite(w.damage_per_level) || w.damage_per_level < 0.0 ||
        w.damage_per_level > 10.0) {
        return "weapon damage_per_level out of range";
    }
    if (!in_range(w.base_damage * (1.0 + w.damage_per_level *
                      static_cast<double>(w.max_level - 1)),
                  0.0, WeaponLimits::kMaxDamage)) {
        return "weapon maximum-level damage out of range";
    }
    const std::string err = validate_weapon_definition(w);
    if (!err.empty()) {
        return err;
    }
    if (find(w.id) != nullptr) {
        return "duplicate weapon id: " + w.id;
    }
    weapons_.push_back(w);
    return std::string();
}

const WeaponDefinition *WeaponRegistry::find(const std::string &id) const {
    for (const WeaponDefinition &w : weapons_) {
        if (w.id == id) {
            return &w;
        }
    }
    return nullptr;
}

WeaponRegistry WeaponRegistry::make_default() {
    WeaponRegistry r;
    WeaponDefinition bubble;
    bubble.id = "bubble_shot";
    bubble.name = "泡泡弹";
    bubble.description = "自动向最近的敌人发射泡泡";
    bubble.max_level = 8;
    bubble.implemented = true;
    bubble.damage_per_level = kBubbleDamagePerLevel;
    bubble.attack_kind = AttackKind::kProjectile;
    r.add(bubble);

    WeaponDefinition sweep;
    sweep.id = "tail_sweep";
    sweep.name = "摆尾波";
    sweep.description = "周期性以自身为中心震荡，击退并伤害范围内所有敌人";
    sweep.max_level = 8;
    sweep.implemented = true;
    sweep.damage_per_level = 0.2;
    sweep.attack_kind = AttackKind::kBurst;
    sweep.base_damage = 10.0;
    sweep.cooldown = 1.6;
    sweep.range = 110.0;
    sweep.area_radius = 110.0;
    sweep.knockback = 40.0;
    sweep.special_effect = "knockback";
    r.add(sweep);

    WeaponDefinition pool;
    pool.id = "data_pool";
    pool.name = "数据涟漪";
    pool.description = "在最近的敌人处生成持续伤害的数据水池";
    pool.max_level = 8;
    pool.implemented = true;
    pool.damage_per_level = 0.2;
    pool.attack_kind = AttackKind::kZone;
    pool.base_damage = 6.0;
    pool.cooldown = 4.0;
    pool.range = 420.0;
    pool.area_radius = 90.0;
    pool.area_lifetime = 3.0;
    pool.tick_interval = 0.5;
    pool.special_effect = "damage over time";
    r.add(pool);
    return r;
}

// --------------------------------------------------------- WeaponInventory

bool WeaponInventory::add(const std::string &id, int level, int max_level) {
    if (!is_valid_identifier(id) || max_level < 1 ||
        max_level > WeaponRegistry::kMaxWeaponLevel || level < 1 ||
        level > max_level || has(id) || full()) {
        return false;
    }
    entries_.push_back(Entry{id, level, max_level});
    return true;
}

bool WeaponInventory::has(const std::string &id) const {
    return level(id) > 0;
}

int WeaponInventory::level(const std::string &id) const {
    for (const Entry &e : entries_) {
        if (e.id == id) {
            return e.level;
        }
    }
    return 0;
}

bool WeaponInventory::can_upgrade(const std::string &id) const {
    for (const Entry &e : entries_) {
        if (e.id == id) {
            return e.level < e.max_level;
        }
    }
    return false;
}

bool WeaponInventory::upgrade(const std::string &id) {
    for (Entry &e : entries_) {
        if (e.id == id) {
            if (e.level >= e.max_level) {
                return false;
            }
            ++e.level;
            return true;
        }
    }
    return false;
}

} // namespace ai_survivors
