#include "core/progression.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ai_survivors {

namespace {

bool finite_vec(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

double dist_sq(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

constexpr double kMaxXpMultiplier = 1000.0;

// Cap a stat can reach through cards; the same numbers the combat core clamps
// to, so an offered card is never a silent no-op.
double stat_cap(StatKind stat, const PlayerRuntimeStats &s) {
    switch (stat) {
    case StatKind::kMaxHp: return PlayerRuntimeStats::kMaxHp;
    case StatKind::kDamage: return PlayerRuntimeStats::kMaxDamageMultiplier;
    case StatKind::kAttackSpeed: return PlayerRuntimeStats::kMaxAttackSpeed;
    case StatKind::kMoveSpeed:
        return s.base_move_speed * PlayerRuntimeStats::kMaxMoveSpeedFactor;
    case StatKind::kArmor: return PlayerRuntimeStats::kMaxArmor;
    case StatKind::kCrit: return PlayerRuntimeStats::kMaxCrit;
    case StatKind::kPickupRadius: return PlayerRuntimeStats::kMaxPickupRadius;
    case StatKind::kNone: break;
    }
    return 0.0;
}

double *stat_ref(StatKind stat, PlayerRuntimeStats &s) {
    switch (stat) {
    case StatKind::kMaxHp: return &s.max_hp;
    case StatKind::kDamage: return &s.damage_multiplier;
    case StatKind::kAttackSpeed: return &s.attack_speed_multiplier;
    case StatKind::kMoveSpeed: return &s.move_speed;
    case StatKind::kArmor: return &s.armor;
    case StatKind::kCrit: return &s.crit_chance;
    case StatKind::kPickupRadius: return &s.pickup_radius;
    case StatKind::kNone: break;
    }
    return nullptr;
}

double stat_delta(const CardDefinition &card, const PlayerRuntimeStats &s) {
    return card.stat == StatKind::kMoveSpeed ? card.amount * s.base_move_speed
                                             : card.amount;
}

// Applies and clamps to the cap. Returns true if the stat value changed.
bool apply_stat(const CardDefinition &card, PlayerRuntimeStats &s) {
    double *ref = stat_ref(card.stat, s);
    if (ref == nullptr) {
        return false;
    }
    const double before = *ref;
    if (!std::isfinite(before) || before >= stat_cap(card.stat, s)) {
        return false;
    }
    *ref = std::min(stat_cap(card.stat, s), before + stat_delta(card, s));
    return *ref != before;
}

} // namespace

// ------------------------------------------------------- PlayerRuntimeStats

PlayerRuntimeStats PlayerRuntimeStats::from_character(
    const CharacterDefinition &c) {
    PlayerRuntimeStats s;
    const auto bounded = [](double value, double low, double high, double fallback) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    // Match the combat core's effective initial values. Otherwise, for example,
    // an attack-speed card could modify 0.01 -> 0.09 while combat remains at its
    // 0.1 minimum, making a supposedly valid reward do nothing.
    s.max_hp = bounded(c.max_hp, 1.0, kMaxHp, 100.0);
    s.move_speed = bounded(c.move_speed, std::numeric_limits<double>::min(),
                          std::numeric_limits<double>::max() / (2.0 * kMaxMoveSpeedFactor), 220.0);
    s.base_move_speed = s.move_speed;
    s.armor = bounded(c.armor, 0.0, kMaxArmor, 0.0);
    s.damage_multiplier = bounded(c.damage_multiplier, 0.0, kMaxDamageMultiplier, 1.0);
    s.attack_speed_multiplier = bounded(c.attack_speed_multiplier, 0.1, kMaxAttackSpeed, 1.0);
    s.crit_chance = bounded(c.crit_chance, 0.0, kMaxCrit, 0.0);
    s.pickup_radius = bounded(c.pickup_radius, 0.0, kMaxPickupRadius, 80.0);
    s.xp_multiplier = bounded(c.xp_multiplier, std::numeric_limits<double>::min(), kMaxXpMultiplier, 1.0);
    return s;
}

CombatStats PlayerRuntimeStats::combat() const {
    CombatStats c;
    c.max_hp = max_hp;
    c.armor = armor;
    c.damage_multiplier = damage_multiplier;
    c.attack_speed_multiplier = attack_speed_multiplier;
    c.crit_chance = crit_chance;
    return c;
}

// -------------------------------------------------------- ExperienceSystem

bool ExperienceSystem::spawn(Vec2 position, double xp) {
    if (!finite_vec(position) || !std::isfinite(xp) || xp <= 0.0) {
        return false;
    }
    xp = std::min(xp, kMaxOrbXp);
    if (orbs_.size() >= kMaxOrbs) {
        std::size_t best = 0;
        double best_d = dist_sq(orbs_[0].position, position);
        for (std::size_t i = 1; i < orbs_.size(); ++i) {
            const double d = dist_sq(orbs_[i].position, position);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        orbs_[best].xp += xp;
        return true;
    }
    orbs_.push_back(XpOrb{position, xp});
    return true;
}

std::size_t ExperienceSystem::spawn_from_kills(
    GameState state, const std::vector<KillEvent> &events) {
    if (state != GameState::kPlaying) {
        return 0;
    }
    std::size_t accepted = 0;
    for (const KillEvent &e : events) {
        if (spawn(e.position, e.xp)) {
            ++accepted;
        }
    }
    return accepted;
}

double ExperienceSystem::collect(GameState state, Vec2 player,
                                 double pickup_radius, double xp_multiplier) {
    if (state != GameState::kPlaying || !finite_vec(player) ||
        !std::isfinite(pickup_radius) || pickup_radius < 0.0 ||
        !std::isfinite(xp_multiplier) || xp_multiplier <= 0.0) {
        return 0.0;
    }
    xp_multiplier = std::min(xp_multiplier, kMaxXpMultiplier);
    const double reach_sq = pickup_radius * pickup_radius;
    double raw = 0.0;
    orbs_.erase(std::remove_if(orbs_.begin(), orbs_.end(),
                               [&](const XpOrb &o) {
                                   if (dist_sq(o.position, player) > reach_sq) {
                                       return false;
                                   }
                                   raw += o.xp;
                                   return true;
                               }),
                orbs_.end());
    return raw * xp_multiplier;
}

double ExperienceSystem::total_xp() const {
    double sum = 0.0;
    for (const XpOrb &o : orbs_) {
        sum += o.xp;
    }
    return sum;
}

// ----------------------------------------------------------- LevelUpSystem

double LevelUpSystem::threshold_for(int level) {
    const int l = std::max(1, std::min(level, kMaxLevel));
    return 10.0 + 5.0 * static_cast<double>(l - 1);
}

int LevelUpSystem::add_xp(double amount) {
    if (!std::isfinite(amount) || amount < 0.0) {
        return -1;
    }
    if (level_ >= kMaxLevel) {
        xp_ = 0.0; // documented: nothing accumulates at max level
        return 0;
    }
    double sum = xp_ + amount;
    if (!std::isfinite(sum)) {
        return -1;
    }
    int gained = 0;
    // Bounded by kMaxLevel iterations regardless of how large sum is.
    while (level_ < kMaxLevel && sum >= threshold_for(level_)) {
        sum -= threshold_for(level_);
        ++level_;
        ++pending_;
        ++gained;
    }
    xp_ = level_ >= kMaxLevel ? 0.0 : sum;
    return gained;
}

bool LevelUpSystem::consume_pending() {
    if (pending_ <= 0) {
        return false;
    }
    --pending_;
    return true;
}

// ------------------------------------------------------ UpgradeCardSystem

UpgradeCardSystem::UpgradeCardSystem(const WeaponRegistry &weapons,
                                     std::uint64_t seed, bool include_defaults)
    : weapons_(weapons), rng_(seed) {
    if (!include_defaults) {
        return;
    }
    const auto attr = [this](const char *id, const char *name, const char *desc,
                             CardRarity rarity, double weight, StatKind stat,
                             double amount) {
        CardDefinition c;
        c.id = id;
        c.name = name;
        c.description = desc;
        c.kind = CardKind::kAttribute;
        c.rarity = rarity;
        c.weight = weight;
        c.max_rank = 10;
        c.stat = stat;
        c.amount = amount;
        add_card(c);
    };
    attr("attr_max_hp", "生命强化", "最大生命 +20，并立即回复 20 点生命",
         CardRarity::kCommon, 10.0, StatKind::kMaxHp, 20.0);
    attr("attr_damage", "伤害强化", "伤害 +10%", CardRarity::kCommon, 10.0,
         StatKind::kDamage, 0.10);
    attr("attr_attack_speed", "攻速强化", "攻击速度 +8%", CardRarity::kCommon,
         10.0, StatKind::kAttackSpeed, 0.08);
    attr("attr_move_speed", "疾风", "移动速度 +8%", CardRarity::kCommon, 10.0,
         StatKind::kMoveSpeed, 0.08);
    attr("attr_armor", "护甲强化", "护甲 +2", CardRarity::kRare, 6.0,
         StatKind::kArmor, 2.0);
    attr("attr_crit", "暴击强化", "暴击率 +3%", CardRarity::kRare, 6.0,
         StatKind::kCrit, 0.03);
    attr("attr_pickup", "磁力", "拾取范围 +15", CardRarity::kCommon, 8.0,
         StatKind::kPickupRadius, 15.0);

    for (const WeaponDefinition &w : weapons_.weapons()) {
        if (!w.implemented) {
            continue;
        }
        CardDefinition up;
        up.id = "weapon_upgrade_" + w.id;
        up.name = w.name + " 强化";
        up.description = w.name + "等级 +1，伤害提升";
        up.kind = CardKind::kWeaponUpgrade;
        up.rarity = CardRarity::kRare;
        up.weight = 12.0;
        up.max_rank = w.max_level;
        up.weapon_id = w.id;
        add_card(up);

        CardDefinition acquire;
        acquire.id = "weapon_acquire_" + w.id;
        acquire.name = "获得" + w.name;
        acquire.description = "获得新武器：" + w.name;
        acquire.kind = CardKind::kWeaponAcquire;
        acquire.rarity = CardRarity::kEpic;
        acquire.weight = 8.0;
        acquire.max_rank = 1;
        acquire.weapon_id = w.id;
        add_card(acquire);
    }
}

std::string UpgradeCardSystem::add_card(const CardDefinition &card) {
    if (card.id.empty() || card.name.empty()) {
        return "card needs id and name";
    }
    if (find(card.id) != nullptr) {
        return "duplicate card id: " + card.id;
    }
    if (!std::isfinite(card.weight) || card.weight <= 0.0) {
        return "card weight must be finite and > 0";
    }
    if (card.max_rank < 1 || card.max_rank > WeaponRegistry::kMaxWeaponLevel) {
        return "card max_rank out of range";
    }
    if (card.kind == CardKind::kAttribute) {
        if (card.stat == StatKind::kNone || !std::isfinite(card.amount) ||
            card.amount <= 0.0) {
            return "attribute card needs a stat and positive amount";
        }
    } else {
        const WeaponDefinition *w = weapons_.find(card.weapon_id);
        if (w == nullptr) {
            return "weapon card references unknown weapon";
        }
    }
    cards_.push_back(card);
    ranks_.push_back(0);
    return std::string();
}

const CardDefinition *UpgradeCardSystem::find(const std::string &id) const {
    for (const CardDefinition &c : cards_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

int UpgradeCardSystem::rank(const std::string &card_id) const {
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        if (cards_[i].id == card_id) {
            return ranks_[i];
        }
    }
    return 0;
}

bool UpgradeCardSystem::would_change(const CardDefinition &card,
                                     const PlayerRuntimeStats &stats) const {
    PlayerRuntimeStats copy = stats;
    return apply_stat(card, copy);
}

bool UpgradeCardSystem::is_available(const CardDefinition &card,
                                     const PlayerRuntimeStats &stats,
                                     const WeaponInventory &inventory) const {
    switch (card.kind) {
    case CardKind::kAttribute:
        return rank(card.id) < card.max_rank && would_change(card, stats);
    case CardKind::kWeaponUpgrade: {
        const WeaponDefinition *w = weapons_.find(card.weapon_id);
        return w != nullptr && w->implemented &&
               inventory.can_upgrade(card.weapon_id);
    }
    case CardKind::kWeaponAcquire: {
        const WeaponDefinition *w = weapons_.find(card.weapon_id);
        return w != nullptr && w->implemented && !inventory.has(card.weapon_id) &&
               !inventory.full();
    }
    }
    return false;
}

std::vector<const CardDefinition *> UpgradeCardSystem::available(
    const PlayerRuntimeStats &stats, const WeaponInventory &inventory) const {
    std::vector<const CardDefinition *> out;
    for (const CardDefinition &c : cards_) {
        if (is_available(c, stats, inventory)) {
            out.push_back(&c);
        }
    }
    return out;
}

UpgradeOffer UpgradeCardSystem::generate(const PlayerRuntimeStats &stats,
                                         const WeaponInventory &inventory) {
    std::vector<const CardDefinition *> pool = available(stats, inventory);
    ++generation_;
    offer_ = UpgradeOffer();
    offer_.generation = generation_;
    while (offer_.card_ids.size() < kOfferSize && !pool.empty()) {
        double max_weight = 0.0;
        for (const CardDefinition *c : pool) {
            max_weight = std::max(max_weight, c->weight);
        }
        double total = 0.0;
        for (const CardDefinition *c : pool) {
            // Normalise before summing so individually finite weights cannot
            // overflow and force every offer to the final entries.
            total += c->weight / max_weight;
        }
        const double roll = rng_.next_unit() * total;
        double acc = 0.0;
        std::size_t pick = pool.size() - 1;
        for (std::size_t i = 0; i < pool.size(); ++i) {
            acc += pool[i]->weight / max_weight;
            if (roll < acc) {
                pick = i;
                break;
            }
        }
        offer_.card_ids.push_back(pool[pick]->id);
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
    }
    offer_active_ = !offer_.card_ids.empty();
    return offer_;
}

ChooseResult UpgradeCardSystem::choose(std::uint64_t generation,
                                       const std::string &card_id,
                                       PlayerRuntimeStats &stats,
                                       WeaponInventory &inventory) {
    if (!offer_active_) {
        return ChooseResult::kNoOffer;
    }
    if (generation != offer_.generation) {
        return ChooseResult::kStaleGeneration;
    }
    if (std::find(offer_.card_ids.begin(), offer_.card_ids.end(), card_id) ==
        offer_.card_ids.end()) {
        return ChooseResult::kNotOffered;
    }
    std::size_t index = cards_.size();
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        if (cards_[i].id == card_id) {
            index = i;
            break;
        }
    }
    if (index == cards_.size() || !is_available(cards_[index], stats, inventory)) {
        return ChooseResult::kInvalid;
    }
    const CardDefinition &card = cards_[index];
    bool ok = false;
    switch (card.kind) {
    case CardKind::kAttribute:
        ok = apply_stat(card, stats);
        break;
    case CardKind::kWeaponUpgrade:
        ok = inventory.upgrade(card.weapon_id);
        break;
    case CardKind::kWeaponAcquire: {
        const WeaponDefinition *w = weapons_.find(card.weapon_id);
        ok = w != nullptr && inventory.add(card.weapon_id, 1, w->max_level);
        break;
    }
    }
    if (!ok) {
        return ChooseResult::kInvalid; // unreachable after is_available
    }
    ++ranks_[index];
    offer_active_ = false;
    return ChooseResult::kApplied;
}

// ------------------------------------------------------ ProgressionSession

ProgressionSession::ProgressionSession(const CharacterDefinition &character,
                                       const WeaponRegistry &weapons,
                                       std::uint64_t seed)
    : weapons_(weapons),
      stats_(PlayerRuntimeStats::from_character(character)),
      cards_(weapons_, seed) {
    const WeaponDefinition *w = weapons_.find(character.initial_weapon_id);
    if (w == nullptr || !w->implemented ||
        !inventory_.add(w->id, 1, w->max_level)) {
        init_error_ = "initial weapon unavailable: " + character.initial_weapon_id;
    }
    const PassiveRegistry passives = PassiveRegistry::make_default();
    const PassiveSpec *passive = passives.find(character.passive_id);
    if (passive == nullptr) {
        if (init_error_.empty()) {
            init_error_ = "unknown passive: " + character.passive_id;
        }
    } else {
        passive_ = PassiveRuntime(*passive);
    }
}

int ProgressionSession::ingest_and_collect(GameState state,
                                           const std::vector<KillEvent> &kills,
                                           Vec2 player) {
    if (state != GameState::kPlaying) {
        return 0;
    }
    orbs_.spawn_from_kills(state, kills);
    const double xp =
        orbs_.collect(state, player, stats_.pickup_radius, stats_.xp_multiplier);
    if (!(xp > 0.0)) {
        return 0;
    }
    passive_.feed(xp);
    return std::max(0, levels_.add_xp(xp));
}

double ProgressionSession::take_passive_heal(GameState state, double max_hp) {
    if (state != GameState::kPlaying) {
        return 0.0;
    }
    return passive_.take_heal(max_hp);
}

int ProgressionSession::grant_xp(GameState state, double raw_xp) {
    if (state != GameState::kPlaying || !std::isfinite(raw_xp) || raw_xp < 0.0) {
        return 0;
    }
    return std::max(0, levels_.add_xp(raw_xp * stats_.xp_multiplier));
}

bool ProgressionSession::advance_offer() {
    while (levels_.pending_levels() > 0) {
        const UpgradeOffer offer = cards_.generate(stats_, inventory_);
        if (!offer.card_ids.empty()) {
            return true;
        }
        levels_.consume_pending(); // empty pool: resolve without a reward
    }
    return false;
}

ChooseResult ProgressionSession::choose(std::uint64_t generation,
                                        const std::string &card_id) {
    if (levels_.pending_levels() <= 0) {
        return ChooseResult::kNoOffer;
    }
    const ChooseResult r = cards_.choose(generation, card_id, stats_, inventory_);
    if (r == ChooseResult::kApplied) {
        levels_.consume_pending();
    }
    return r;
}

std::string ProgressionSession::compile_loadout(
    std::vector<WeaponSpec> &out) const {
    return ai_survivors::compile_loadout(weapons_, inventory_, out);
}

double ProgressionSession::weapon_damage_scale() const {
    const WeaponDefinition *w = weapons_.find("bubble_shot");
    const int level = inventory_.level("bubble_shot");
    if (w == nullptr || level < 1) {
        return 1.0;
    }
    return 1.0 + w->damage_per_level * static_cast<double>(level - 1);
}

} // namespace ai_survivors
