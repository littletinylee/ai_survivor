#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ai_survivors {

// How a weapon attacks. Generic machinery switches on this, never on an id.
enum class AttackKind {
    kProjectile, // fires projectile_count shots at the nearest target
    kBurst,      // instant area hit centred on the player
    kZone,       // stationary damaging zone placed at the nearest target
};

struct WeaponDefinition {
    std::string id;
    std::string name;
    std::string description;
    int max_level = 8;
    // True only if the scene has real gameplay for this weapon. Unimplemented
    // definitions may exist in the registry but are never offered.
    bool implemented = false;
    // Damage multiplier gain per level above 1 (bubble: +20% of base).
    double damage_per_level = 0.0;

    AttackKind attack_kind = AttackKind::kProjectile;
    double base_damage = 12.0;       // per hit / per zone tick, before level
    double cooldown = 0.8;           // seconds at attack speed 1.0
    double range = 520.0;            // target / cast range
    int projectile_count = 1;        // projectile kind only
    double projectile_speed = 560.0;
    double projectile_radius = 8.0;
    double projectile_lifetime = 1.4;
    int pierce_count = 0;            // extra enemies a projectile passes through
    double knockback = 0.0;          // distance units pushed per hit
    std::string special_effect;      // descriptive text only
    double area_radius = 0.0;        // burst / zone radius
    double area_lifetime = 0.0;      // zone lifetime
    double tick_interval = 0.0;      // zone tick period
};

// Bounds shared by validation and combat.
struct WeaponLimits {
    static constexpr int kMaxProjectileCount = 8;
    static constexpr int kMaxPierce = 8;
    static constexpr double kMaxDamage = 1e6;
    static constexpr double kMinCooldown = 0.05;
    static constexpr double kMaxCooldown = 120.0;
    static constexpr double kMaxRange = 2000.0;
    static constexpr double kMaxSpeed = 5000.0;
    static constexpr double kMaxRadius = 1000.0;
    static constexpr double kMaxLifetime = 30.0;
    static constexpr double kMinTick = 0.05;
    static constexpr double kMaxKnockback = 200.0;
};

// Empty string when the definition is internally consistent and bounded.
std::string validate_weapon_definition(const WeaponDefinition &weapon);

class WeaponRegistry {
public:
    static constexpr int kMaxWeaponLevel = 100;

    // Empty string on success; on failure nothing is added.
    std::string add(const WeaponDefinition &weapon);
    const WeaponDefinition *find(const std::string &id) const;
    const std::vector<WeaponDefinition> &weapons() const { return weapons_; }

    // bubble_shot, tail_sweep and data_pool; all implemented.
    static WeaponRegistry make_default();

private:
    std::vector<WeaponDefinition> weapons_;
};

class WeaponInventory {
public:
    static constexpr std::size_t kMaxWeapons = 6;

    struct Entry {
        std::string id;
        int level = 1;
        int max_level = 1;
    };

    // Rejects invalid id, level outside [1, max_level], max_level outside
    // [1, WeaponRegistry::kMaxWeaponLevel], duplicates, and a 7th weapon.
    bool add(const std::string &id, int level, int max_level);
    bool has(const std::string &id) const;
    int level(const std::string &id) const; // 0 when absent
    bool full() const { return entries_.size() >= kMaxWeapons; }
    bool can_upgrade(const std::string &id) const;
    // Rejects absent or already maxed weapons.
    bool upgrade(const std::string &id);
    const std::vector<Entry> &entries() const { return entries_; }
    void clear() { entries_.clear(); }

private:
    std::vector<Entry> entries_;
};

// A definition resolved at a level: what combat actually executes. Player
// multipliers (damage, attack speed, crit) are NOT baked in; combat applies
// them at fire time so they can never be counted twice.
struct WeaponSpec {
    std::string id;
    int level = 1;
    AttackKind kind = AttackKind::kProjectile;
    double damage = 12.0;
    double cooldown = 0.8;
    double range = 520.0;
    int projectile_count = 1;
    double projectile_speed = 560.0;
    double projectile_radius = 8.0;
    double projectile_lifetime = 1.4;
    int pierce_count = 0;
    double knockback = 0.0;
    double area_radius = 0.0;
    double area_lifetime = 0.0;
    double tick_interval = 0.0;
};

// Empty string when the spec is bounded and consistent for its kind.
std::string validate_weapon_spec(const WeaponSpec &spec);

// Resolves every inventory entry against the registry. Atomic: on any error
// `out` is left untouched and the message is returned. Entries must be
// implemented, known and valid; the loadout must hold 1..6 unique weapons.
std::string compile_loadout(const WeaponRegistry &registry,
                            const WeaponInventory &inventory,
                            std::vector<WeaponSpec> &out);

} // namespace ai_survivors
