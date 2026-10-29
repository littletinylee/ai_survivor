#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/character.h"
#include "core/game_state.h"
#include "core/player_controller.h"
#include "core/weapon.h"

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

enum class EnemyKind {
    kChaser = 0, // walks straight at the player
    kCharger,    // telegraphed dash, see ChargerTuning
    kBrute,      // chaser behaviour; bulky body, set by the spawn stats
};

enum class ChargerPhase {
    kApproach = 0,
    kWindup,   // stationary, aim locked at its start
    kDash,     // straight along the locked aim, never homes
    kRecovery, // stationary
};

// Charger timings (seconds) and ranges. Only used by EnemyKind::kCharger.
struct ChargerTuning {
    static constexpr double kTriggerRange = 220.0;
    static constexpr double kWindupSeconds = 0.7;
    static constexpr double kDashSeconds = 0.3;
    static constexpr double kDashSpeedMultiplier = 3.0;
    static constexpr double kRecoverySeconds = 0.8;
    // Minimum approach time after a recovery before the next windup may start.
    static constexpr double kRearmSeconds = 1.0;
    // Largest delta one charger consumes per step call (hostile-dt guard).
    static constexpr double kMaxStepDelta = 0.25;
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
    EnemyKind kind = EnemyKind::kChaser;
    // Charger state, read-only for rendering. Other kinds stay in kApproach.
    ChargerPhase phase = ChargerPhase::kApproach;
    double phase_remaining = 0.0; // seconds left in windup/dash/recovery
    double rearm_remaining = 0.0; // seconds before the next windup may begin
    Vec2 aim{1.0, 0.0};           // unit vector locked at windup start
};

struct Projectile {
    Vec2 position;
    Vec2 velocity;
    double damage = 0.0;
    double radius = 8.0;
    double remaining_life = 0.0;
    bool critical = false;
    int pierce_left = 0;      // further enemies it may pass through
    double knockback = 0.0;
    // Enemies already hit; bounded by pierce_count + 1 because the projectile
    // retires on its last allowed hit. Prevents repeat hits while overlapping.
    std::vector<std::uint32_t> hit_ids;
};

// Stationary damaging area. Ticks at tick_interval, 2*tick_interval, ... while
// age <= lifetime, then is removed. Damage per tick is fixed at spawn.
struct Zone {
    Vec2 position;
    double radius = 0.0;
    double damage = 0.0;
    double knockback = 0.0;
    double tick_interval = 0.5;
    double lifetime = 0.0;
    double age = 0.0;
    double next_tick = 0.5; // age at which the next tick fires
    bool critical = false;
};

// Instant area hit around the player, resolved by CombatWorld in the same
// substep it was produced.
struct BurstAttack {
    Vec2 center;
    double radius = 0.0;
    double damage = 0.0;
    double knockback = 0.0;
    bool critical = false;
};

// Read-only transient ring for rendering.
struct BurstVisual {
    static constexpr double kDuration = 0.25;
    Vec2 center;
    double radius = 0.0;
    double age = 0.0;
};

// Where area weapons put their output. Null zones means area weapons cannot
// fire (and therefore spend no cooldown).
struct AreaSink {
    static constexpr std::size_t kMaxZones = 32;
    std::vector<BurstAttack> bursts;
    std::vector<Zone> *zones = nullptr;
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
    EnemyKind kind = EnemyKind::kChaser;
    // Body radius override; 0 means "use the radius passed to spawn()"
    // (CombatConfig::enemy_radius for CombatWorld). Must be 0 or in (0, 100].
    double radius = 0.0;
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

    static constexpr double kMaxSpawnRadius = 100.0;

    // Returns false (and spawns nothing) at capacity, for a non-finite
    // position, an unknown kind, or a non-finite/negative/oversized
    // spec.radius. radius is the default body radius when spec.radius is 0.
    bool spawn(const EnemySpawn &spec, double radius);
    // Chasers/brutes walk straight at target (unbounded). Chargers advance
    // their telegraph state machine.
    void step(double dt, Vec2 target);
    // Same, but charger dashes are clamped to the arena rectangle.
    void step(double dt, Vec2 target, double arena_half_width,
              double arena_half_height);
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

// Runs every equipped weapon with its own independent cooldown. Behaviour is
// chosen by AttackKind, never by weapon id.
class WeaponManager {
public:
    struct Slot {
        WeaponSpec spec;
        double interval = 0.8; // spec.cooldown at the current attack speed
        double cooldown = 0.0;
    };

    // Legacy bare configuration: a single projectile weapon taken from the
    // CombatConfig scalars (id "bubble_shot"), starting ready.
    void configure(const CombatStats &stats, const CombatConfig &config);
    void reset();
    void reconfigure(const CombatStats &stats, const CombatConfig &config);
    // Replaces the loadout from validated specs (atomic: an invalid or
    // oversized/duplicated list changes nothing and returns false). Slots whose
    // id already exists update in place and keep their cooldown fraction; new
    // ids start ready; ids that vanished are dropped.
    bool set_loadout(const CombatStats &stats,
                     const std::vector<WeaponSpec> &specs);
    // New player stats, same loadout; cooldown fractions are preserved.
    void update_stats(const CombatStats &stats);
    // Cools down every slot and fires each ready one that has a target and
    // room. Returns the number of weapons that fired. Area weapons need `area`.
    int step_all(double dt, Vec2 origin, const EnemyManager &enemies,
                 ProjectileSystem &projectiles, CombatRng &rng, AreaSink *area);
    // Single-weapon convenience used by bare tests: true if anything fired.
    bool step(double dt, Vec2 origin, const EnemyManager &enemies,
              ProjectileSystem &projectiles, CombatRng &rng,
              AreaSink *area = nullptr) {
        return step_all(dt, origin, enemies, projectiles, rng, area) > 0;
    }
    // Primary (first) weapon, for the legacy accessors.
    double cooldown_remaining() const {
        return slots_.empty() ? 0.0 : slots_[0].cooldown;
    }
    double interval() const { return slots_.empty() ? 0.0 : slots_[0].interval; }
    const std::vector<Slot> &slots() const { return slots_; }

private:
    bool fire(Slot &slot, Vec2 origin, const EnemyManager &enemies,
              ProjectileSystem &projectiles, CombatRng &rng, AreaSink *area);

    CombatStats stats_;
    std::vector<Slot> slots_;
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

// Wave introduction times (combat seconds); before them every spawn is a chaser.
constexpr double kChargerIntroSeconds = 45.0;
constexpr double kBruteIntroSeconds = 90.0;

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

    // Installs the real equipped loadout (from compile_loadout). Atomic: an
    // empty, oversized, duplicate or invalid list is rejected with no change.
    // Existing weapons update in place (cooldown fractions, enemies, RNG, HP
    // untouched); newly present weapons start ready. A bare CombatWorld keeps
    // its single config-driven bubble weapon until this is first called.
    bool set_loadout(const std::vector<WeaponSpec> &specs);

    // Restores HP. Only a finite positive amount, only while the player is
    // alive and `state` is Playing; clamps to max HP and returns the HP
    // actually restored (0 on any rejection). This is the only way to add HP
    // besides a max HP increase in update_player_stats; it never revives.
    double heal(GameState state, double amount);

    // Moves pending kill events out; the internal buffer is emptied.
    std::vector<KillEvent> take_kill_events();

    const std::vector<Zone> &zones() const { return zones_; }
    const std::vector<BurstVisual> &bursts() const { return bursts_; }
    const std::vector<WeaponManager::Slot> &weapon_slots() const {
        return weapon_.slots();
    }

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
    // Primary weapon's per-hit base damage (before player multipliers).
    double weapon_damage() const {
        return legacy_loadout_ || weapon_.slots().empty()
                   ? config_.weapon_base_damage
                   : weapon_.slots()[0].spec.damage;
    }
    double weapon_cooldown() const { return weapon_.cooldown_remaining(); }
    double weapon_interval() const { return weapon_.interval(); }
    std::uint32_t kill_count() const { return kill_count_; }
    std::uint32_t dropped_kill_events() const { return dropped_kill_events_; }
    double combat_time() const { return combat_time_; }
    const CombatConfig &config() const { return config_; }

private:
    void substep(double h, Vec2 player, GameStateManager &states, bool &died);
    void resolve_projectiles();
    void resolve_bursts(const std::vector<BurstAttack> &attacks);
    void step_zones(double h);
    void step_visuals(double h);
    // Single damage path for projectiles, bursts and zones. Skips dead
    // enemies; records the kill (count + event) exactly once. Returns true if
    // the hit landed. Knockback pushes survivors away from `source`.
    bool damage_enemy(Enemy &enemy, double damage, Vec2 source,
                      double knockback);
    void purge_dead();
    bool resolve_contacts(double h, Vec2 player);

    CombatStats stats_;
    CombatConfig config_;
    CombatRng rng_;
    EnemyManager enemies_;
    ProjectileSystem projectiles_;
    WeaponManager weapon_;
    WaveDirector waves_;
    std::vector<KillEvent> kill_events_;
    std::vector<Zone> zones_;
    std::vector<BurstVisual> bursts_;
    static constexpr std::size_t kMaxBurstVisuals = 64;
    bool legacy_loadout_ = true;
    double base_weapon_damage_ = 12.0;
    double hp_ = 0.0;
    double invulnerable_ = 0.0;
    double combat_time_ = 0.0;
    std::uint32_t kill_count_ = 0;
    std::uint32_t dropped_kill_events_ = 0;
};

} // namespace ai_survivors
