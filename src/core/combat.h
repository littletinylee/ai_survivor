#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/character.h"
#include "core/game_state.h"
#include "core/player_controller.h"

namespace ai_survivors {

// Deterministic splitmix64 generator. Same seed -> same sequence on every
// platform, so combat outcomes are reproducible.
class CombatRng {
public:
    explicit CombatRng(std::uint64_t seed = 1) : state_(seed) {}
    std::uint64_t next();
    // Uniform in [0, 1).
    double next_unit();

private:
    std::uint64_t state_;
};

// Player-side numbers injected from the selected CharacterDefinition.
struct CombatStats {
    double max_hp = 100.0;
    double armor = 0.0;
    double damage_multiplier = 1.0;
    double attack_speed_multiplier = 1.0;
    double crit_chance = 0.0;

    static CombatStats from_character(const CharacterDefinition &character);
};

struct CombatConfig {
    double arena_half_width = 520.0;
    double arena_half_height = 290.0;
    double player_radius = 54.0;

    double weapon_base_damage = 12.0;
    double weapon_base_interval = 0.8; // seconds at attack speed 1.0
    double weapon_range = 520.0;
    double projectile_speed = 560.0;
    double projectile_radius = 8.0;
    double projectile_lifetime = 1.4;

    double enemy_radius = 18.0;
    double contact_interval = 1.0;       // per enemy
    double player_invulnerability = 0.25; // after any contact hit

    // Disabled only by tests/tools that need a hand-placed scenario.
    bool waves_enabled = true;
};

struct Enemy {
    std::uint32_t id = 0;
    Vec2 position;
    double hp = 20.0;
    double max_hp = 20.0;
    double radius = 18.0;
    double speed = 70.0;
    double contact_damage = 8.0;
    double xp_value = 5.0;
    double contact_cooldown = 0.0; // seconds until this enemy may hit again
};

struct Projectile {
    Vec2 position;
    Vec2 velocity;
    double damage = 0.0;
    double radius = 8.0;
    double remaining_life = 0.0;
    bool critical = false;
};

// Emitted once per killed enemy. XP is only a payload; experience itself is
// implemented by a later task.
struct KillEvent {
    Vec2 position;
    double xp = 0.0;
    std::uint32_t enemy_id = 0;
};

struct EnemySpawn {
    Vec2 position;
    double hp = 20.0;
    double speed = 70.0;
    double contact_damage = 8.0;
    double xp_value = 5.0;
};

class DamageSystem {
public:
    static constexpr double kMinDamageMultiplier = 0.0;
    static constexpr double kMaxDamageMultiplier = 10.0;
    static constexpr double kMaxArmor = 1000.0;
    static constexpr double kCritMultiplier = 2.0;
    static constexpr double kMinHitDamage = 1.0;

    // Armor reduction 100 / (100 + armor); any positive hit deals at least
    // kMinHitDamage. Non-finite or non-positive raw damage deals 0.
    static double mitigate(double raw, double armor);
    // Crit chance is clamped to [0, 1]; always consumes exactly one RNG draw.
    static bool roll_crit(double chance, CombatRng &rng);
    static double outgoing_damage(double base, double multiplier, bool critical);
};

class EnemyManager {
public:
    static constexpr std::size_t kMaxEnemies = 256;

    // Returns false (and spawns nothing) when at capacity.
    bool spawn(const EnemySpawn &spec, double radius);
    void step(double dt, Vec2 target);
    // Index of the nearest live enemy within max_range of from, or -1.
    int nearest(Vec2 from, double max_range) const;
    void clear();

    std::vector<Enemy> &mutable_enemies() { return enemies_; }
    const std::vector<Enemy> &enemies() const { return enemies_; }

private:
    std::vector<Enemy> enemies_;
    std::uint32_t next_id_ = 1;
};

class ProjectileSystem {
public:
    static constexpr std::size_t kMaxProjectiles = 512;

    bool spawn(const Projectile &projectile);
    // Moves projectiles and drops those whose lifetime ran out.
    void step(double dt);
    void clear();

    std::vector<Projectile> &mutable_projectiles() { return projectiles_; }
    const std::vector<Projectile> &projectiles() const { return projectiles_; }

private:
    std::vector<Projectile> projectiles_;
};

class WeaponManager {
public:
    void configure(const CombatStats &stats, const CombatConfig &config);
    void reset();
    // Applies new stats/config in place. The remaining cooldown keeps its
    // fraction of the interval, so an upgrade neither refires instantly nor
    // stalls the weapon.
    void reconfigure(const CombatStats &stats, const CombatConfig &config);
    // Cools down, then fires at the nearest enemy in range if ready and the
    // projectile pool has room. Returns true if a shot was fired.
    bool step(double dt, Vec2 origin, const EnemyManager &enemies,
              ProjectileSystem &projectiles, CombatRng &rng);
    double cooldown_remaining() const { return cooldown_; }
    double interval() const { return interval_; }

private:
    CombatStats stats_;
    CombatConfig config_;
    double interval_ = 0.8;
    double cooldown_ = 0.0;
};

class WaveDirector {
public:
    void configure(const CombatConfig &config);
    void reset();
    // elapsed is combat time consumed so far. Spawns at most one batch per call.
    void step(double dt, double elapsed, EnemyManager &enemies, CombatRng &rng);

private:
    CombatConfig config_;
    double accumulator_ = 0.0;
};

// Orchestrates one run's combat. It never owns a victory timer: GameTimer stays
// the only 1200 s clock. The combat clock below only schedules waves and is fed
// by the same Playing-only deltas.
//
// Frame order required of the caller (death priority): movement, then
// CombatWorld::tick, then GameTimer::advance only if still Playing. A lethal
// hit in the same frame as the 1200 s boundary therefore wins over victory.
class CombatWorld {
public:
    enum class TickResult {
        kRejected,   // dt/player position non-finite or dt negative
        kFrozen,     // state is not Playing (paused, upgrade, terminal, menu)
        kAdvanced,
        kPlayerDied, // this tick brought HP to 0 and moved state to Lost
    };

    // Largest delta consumed per call; larger deltas are clamped (not
    // rejected) so a hitch cannot teleport the simulation. Work is divided
    // into at most ceil(kMaxAcceptedDelta / kSubstep) = 15 substeps.
    static constexpr double kMaxAcceptedDelta = 0.25;
    static constexpr double kSubstep = 1.0 / 60.0;
    static constexpr std::size_t kMaxKillEvents = 256;

    CombatWorld(const CombatStats &stats, std::uint64_t seed,
                const CombatConfig &config = CombatConfig());

    TickResult tick(GameStateManager &states, double dt, Vec2 player_position);

    // Spawns a hand-specified enemy (also used by WaveDirector's path).
    bool spawn_enemy(const EnemySpawn &spec);

    // Applies upgraded player stats without rebuilding enemies, projectiles or
    // RNG. Rejects (no change) if any value is non-finite or weapon_damage_scale
    // is not positive. A max HP increase heals exactly the added amount; a
    // decrease only clamps HP. Nothing else refills HP. weapon_damage_scale
    // multiplies the damage the world was constructed with.
    bool update_player_stats(const CombatStats &stats,
                             double weapon_damage_scale);

    // Moves pending kill events out; the internal buffer is emptied.
    std::vector<KillEvent> take_kill_events();

    const std::vector<Enemy> &enemies() const { return enemies_.enemies(); }
    const std::vector<Projectile> &projectiles() const {
        return projectiles_.projectiles();
    }
    const std::vector<KillEvent> &pending_kill_events() const {
        return kill_events_;
    }
    double hp() const { return hp_; }
    double max_hp() const { return stats_.max_hp; }
    const CombatStats &stats() const { return stats_; }
    double weapon_damage() const { return config_.weapon_base_damage; }
    double weapon_cooldown() const { return weapon_.cooldown_remaining(); }
    double weapon_interval() const { return weapon_.interval(); }
    std::uint32_t kill_count() const { return kill_count_; }
    std::uint32_t dropped_kill_events() const { return dropped_kill_events_; }
    double combat_time() const { return combat_time_; }
    const CombatConfig &config() const { return config_; }

private:
    void substep(double h, Vec2 player, GameStateManager &states, bool &died);
    void resolve_projectiles();
    bool resolve_contacts(double h, Vec2 player);

    CombatStats stats_;
    CombatConfig config_;
    CombatRng rng_;
    EnemyManager enemies_;
    ProjectileSystem projectiles_;
    WeaponManager weapon_;
    WaveDirector waves_;
    std::vector<KillEvent> kill_events_;
    double base_weapon_damage_ = 12.0;
    double hp_ = 0.0;
    double invulnerable_ = 0.0;
    double combat_time_ = 0.0;
    std::uint32_t kill_count_ = 0;
    std::uint32_t dropped_kill_events_ = 0;
};

} // namespace ai_survivors
