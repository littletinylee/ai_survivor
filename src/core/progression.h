#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/character.h"
#include "core/combat.h"
#include "core/game_state.h"
#include "core/player_controller.h"

namespace ai_survivors {

// Mutable per-run player numbers. CharacterDefinition stays immutable; cards
// only ever change this copy.
struct PlayerRuntimeStats {
    static constexpr double kMaxHp = 1e6;
    static constexpr double kMaxArmor = DamageSystem::kMaxArmor;
    static constexpr double kMaxDamageMultiplier = DamageSystem::kMaxDamageMultiplier;
    static constexpr double kMaxAttackSpeed = 5.0; // matches CombatWorld clamp
    static constexpr double kMaxCrit = 1.0;
    static constexpr double kMaxMoveSpeedFactor = 3.0; // of the base speed
    static constexpr double kMaxPickupRadius = 1000.0;

    double max_hp = 100.0;
    double move_speed = 220.0;
    double base_move_speed = 220.0;
    double armor = 0.0;
    double damage_multiplier = 1.0;
    double attack_speed_multiplier = 1.0;
    double crit_chance = 0.0;
    double pickup_radius = 80.0;
    double xp_multiplier = 1.0;

    static PlayerRuntimeStats from_character(const CharacterDefinition &c);
    CombatStats combat() const;
};

// ------------------------------------------------------------------ XP orbs

struct XpOrb {
    Vec2 position;
    double xp = 0.0; // raw value, before the character XP multiplier
};

// Bounded pool of collectable XP. Everything is frozen unless the state is
// Playing: no orbs spawn and none are collected.
class ExperienceSystem {
public:
    static constexpr std::size_t kMaxOrbs = 512;
    static constexpr double kMaxOrbXp = 1e6; // per kill event, sanitised

    // Adds one orb. Rejects non-finite position / non-positive or non-finite
    // xp. When the pool is full the xp is merged into the nearest orb, so the
    // pool never grows and no xp is lost.
    bool spawn(Vec2 position, double xp);
    // Spawns one orb per event if Playing; returns the number accepted.
    // Events are not consumed by this call (caller owns the vector).
    std::size_t spawn_from_kills(GameState state,
                                 const std::vector<KillEvent> &events);
    // Removes orbs within pickup_radius (inclusive) of the player and returns
    // their xp times xp_multiplier. Returns 0 and changes nothing when not
    // Playing or when any input is non-finite / out of range.
    double collect(GameState state, Vec2 player, double pickup_radius,
                   double xp_multiplier);
    void clear() { orbs_.clear(); }

    // Read-only view for drawing.
    const std::vector<XpOrb> &orbs() const { return orbs_; }
    double total_xp() const;

private:
    std::vector<XpOrb> orbs_;
};

// ------------------------------------------------------------ level / XP bar

// Level 1 start. Threshold to leave level L is 10 + 5 * (L - 1) XP.
// Every level crossed by one grant is queued as a pending upgrade. At
// kMaxLevel XP no longer accumulates: surplus is discarded (documented
// behaviour) and no further levels are queued.
class LevelUpSystem {
public:
    static constexpr int kMaxLevel = 1000;

    static double threshold_for(int level);

    // Rejects (no mutation) negative / non-finite amounts or a non-finite sum.
    // Returns the number of levels gained, or -1 on rejection.
    int add_xp(double amount);

    int level() const { return level_; }
    double xp() const { return xp_; }
    double next_threshold() const { return threshold_for(level_); }
    int pending_levels() const { return pending_; }
    bool at_max_level() const { return level_ >= kMaxLevel; }
    // Resolves one queued level. False when nothing is pending.
    bool consume_pending();

private:
    int level_ = 1;
    double xp_ = 0.0;
    int pending_ = 0;
};

// ----------------------------------------------------------------- weapons

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
};

class WeaponRegistry {
public:
    static constexpr int kMaxWeaponLevel = 100;

    // Empty string on success; on failure nothing is added.
    std::string add(const WeaponDefinition &weapon);
    const WeaponDefinition *find(const std::string &id) const;
    const std::vector<WeaponDefinition> &weapons() const { return weapons_; }

    // Registry containing the single implemented weapon, bubble_shot (max 8).
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

// ------------------------------------------------------------------- cards

enum class CardKind { kAttribute, kWeaponUpgrade, kWeaponAcquire };
enum class CardRarity { kCommon, kRare, kEpic };
enum class StatKind {
    kNone,
    kMaxHp,
    kDamage,
    kAttackSpeed,
    kMoveSpeed, // amount is a fraction of the character's base move speed
    kArmor,
    kCrit,
    kPickupRadius,
};

struct CardDefinition {
    std::string id;
    std::string name;
    std::string description;
    CardKind kind = CardKind::kAttribute;
    CardRarity rarity = CardRarity::kCommon;
    double weight = 10.0;
    int max_rank = 10; // attribute cards only; weapon cards use weapon levels
    StatKind stat = StatKind::kNone;
    double amount = 0.0;
    std::string weapon_id; // weapon cards only
};

struct UpgradeOffer {
    std::uint64_t generation = 0;
    std::vector<std::string> card_ids; // 0..3 unique ids
};

enum class ChooseResult {
    kApplied,
    kNoOffer,       // nothing is currently offered (or already chosen)
    kStaleGeneration,
    kNotOffered,    // id is not part of the current offer
    kInvalid,       // card no longer applicable; offer stays open
};

// Weighted offers without replacement from a fixed seed. Pure data; the
// caller supplies the stats and inventory that a card mutates.
class UpgradeCardSystem {
public:
    static constexpr std::size_t kOfferSize = 3;

    // include_defaults adds the seven attribute cards plus, for every
    // *implemented* registry weapon, an upgrade and an acquire card.
    UpgradeCardSystem(const WeaponRegistry &weapons, std::uint64_t seed,
                      bool include_defaults = true);

    // Empty string on success. Rejects empty/duplicate ids, non-finite or
    // non-positive weight, bad rank, unknown weapon ids.
    std::string add_card(const CardDefinition &card);

    bool is_available(const CardDefinition &card,
                      const PlayerRuntimeStats &stats,
                      const WeaponInventory &inventory) const;
    std::vector<const CardDefinition *> available(
        const PlayerRuntimeStats &stats, const WeaponInventory &inventory) const;

    // Draws min(3, pool) unique cards and makes them the active offer (a new
    // generation id always supersedes the previous offer). An empty pool
    // yields an empty offer that is not choosable.
    UpgradeOffer generate(const PlayerRuntimeStats &stats,
                          const WeaponInventory &inventory);

    // Validates generation + id against the active offer and the card against
    // the current state, then applies it. Any rejection mutates nothing.
    ChooseResult choose(std::uint64_t generation, const std::string &card_id,
                        PlayerRuntimeStats &stats, WeaponInventory &inventory);

    bool has_active_offer() const { return offer_active_; }
    const UpgradeOffer &current_offer() const { return offer_; }
    std::uint64_t generation() const { return generation_; }
    int rank(const std::string &card_id) const;
    const CardDefinition *find(const std::string &id) const;
    const std::vector<CardDefinition> &cards() const { return cards_; }

private:
    bool would_change(const CardDefinition &card,
                      const PlayerRuntimeStats &stats) const;

    std::vector<CardDefinition> cards_;
    std::vector<int> ranks_;
    WeaponRegistry weapons_;
    CombatRng rng_;
    UpgradeOffer offer_;
    bool offer_active_ = false;
    std::uint64_t generation_ = 0;
};

// ----------------------------------------------------------------- session

// One run's progression: orbs, level, inventory, cards and runtime stats.
// Construct a new session for each run (restart drops everything).
class ProgressionSession {
public:
    ProgressionSession(const CharacterDefinition &character,
                       const WeaponRegistry &weapons, std::uint64_t seed);

    // Non-empty when the character's initial weapon is unknown or not
    // implemented; the run must not start.
    const std::string &init_error() const { return init_error_; }

    // Orbs from kills (Playing only), then pickups. Returns levels gained.
    int ingest_and_collect(GameState state,
                           const std::vector<KillEvent> &kills, Vec2 player);
    // Legitimate progression grant (visual test / tools): XP skips the orbs
    // but still passes the character multiplier. Playing only.
    int grant_xp(GameState state, double raw_xp);

    bool needs_upgrade() const { return levels_.pending_levels() > 0; }
    // Starts the next offer for a pending level. Pending levels whose pool is
    // empty are resolved without any reward. Returns true when an offer is
    // active (UI must show it); false when no pending levels remain.
    bool advance_offer();
    ChooseResult choose(std::uint64_t generation, const std::string &card_id);

    double weapon_damage_scale() const;

    ExperienceSystem &experience() { return orbs_; }
    const ExperienceSystem &experience() const { return orbs_; }
    const LevelUpSystem &levels() const { return levels_; }
    const WeaponInventory &inventory() const { return inventory_; }
    const PlayerRuntimeStats &stats() const { return stats_; }
    const UpgradeCardSystem &cards() const { return cards_; }
    const WeaponRegistry &weapons() const { return weapons_; }

private:
    WeaponRegistry weapons_;
    PlayerRuntimeStats stats_;
    ExperienceSystem orbs_;
    LevelUpSystem levels_;
    WeaponInventory inventory_;
    UpgradeCardSystem cards_;
    std::string init_error_;
};

} // namespace ai_survivors
