#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/combat.h"
#include "core/game_timer.h"
#include "core/progression.h"

using namespace ai_survivors;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

CharacterDefinition fish() {
    CharacterDefinition c;
    c.id = "test_fish";
    c.name = "Fish";
    c.description = "d";
    c.max_hp = 100.0;
    c.move_speed = 200.0;
    c.pickup_radius = 80.0;
    c.initial_weapon_id = "bubble_shot";
    c.passive_id = "p";
    c.passive_description = "p";
    return c;
}

WeaponRegistry registry_with_extras(int extra) {
    WeaponRegistry r = WeaponRegistry::make_default();
    for (int i = 0; i < extra; ++i) {
        WeaponDefinition w;
        w.id = "extra_" + std::to_string(i);
        w.name = "Extra";
        w.max_level = 3;
        w.implemented = true;
        EXPECT_TRUE(r.add(w).empty());
    }
    return r;
}

CardDefinition attr_card(const std::string &id, StatKind stat, double amount,
                         int max_rank = 10) {
    CardDefinition c;
    c.id = id;
    c.name = id;
    c.weight = 5.0;
    c.max_rank = max_rank;
    c.stat = stat;
    c.amount = amount;
    return c;
}

} // namespace

TEST(ExperienceOrbs, DropPickupDistanceAndMultiplier) {
    ExperienceSystem xp;
    ASSERT_TRUE(xp.spawn(Vec2{100.0, 0.0}, 5.0));
    ASSERT_TRUE(xp.spawn(Vec2{300.0, 0.0}, 5.0));
    EXPECT_EQ(xp.collect(GameState::kPlaying, Vec2{}, 99.0, 1.0), 0.0);
    EXPECT_EQ(xp.orbs().size(), 2u);
    // Inclusive radius; multiplier applies to collected value only.
    EXPECT_DOUBLE_EQ(xp.collect(GameState::kPlaying, Vec2{}, 100.0, 1.5), 7.5);
    ASSERT_EQ(xp.orbs().size(), 1u);
    EXPECT_DOUBLE_EQ(xp.orbs()[0].xp, 5.0);
}

TEST(ExperienceOrbs, SpawnFromKillEventsOnlyWhilePlaying) {
    ExperienceSystem xp;
    std::vector<KillEvent> kills{{Vec2{1, 2}, 4.0, 1}, {Vec2{3, 4}, 6.0, 2}};
    EXPECT_EQ(xp.spawn_from_kills(GameState::kPaused, kills), 0u);
    EXPECT_EQ(xp.spawn_from_kills(GameState::kUpgrade, kills), 0u);
    EXPECT_EQ(xp.spawn_from_kills(GameState::kLost, kills), 0u);
    EXPECT_TRUE(xp.orbs().empty());
    EXPECT_EQ(xp.spawn_from_kills(GameState::kPlaying, kills), 2u);
    EXPECT_DOUBLE_EQ(xp.total_xp(), 10.0);
}

TEST(ExperienceOrbs, FrozenOutsidePlaying) {
    ExperienceSystem xp;
    ASSERT_TRUE(xp.spawn(Vec2{}, 5.0));
    for (GameState s : {GameState::kMenu, GameState::kPaused, GameState::kUpgrade,
                        GameState::kWon, GameState::kLost}) {
        EXPECT_EQ(xp.collect(s, Vec2{}, 100.0, 1.0), 0.0);
    }
    EXPECT_EQ(xp.orbs().size(), 1u);
}

TEST(ExperienceOrbs, InvalidInputsRejectedWithoutMutation) {
    ExperienceSystem xp;
    EXPECT_FALSE(xp.spawn(Vec2{kNaN, 0}, 1.0));
    EXPECT_FALSE(xp.spawn(Vec2{0, kInf}, 1.0));
    EXPECT_FALSE(xp.spawn(Vec2{}, kNaN));
    EXPECT_FALSE(xp.spawn(Vec2{}, -1.0));
    EXPECT_FALSE(xp.spawn(Vec2{}, 0.0));
    ASSERT_TRUE(xp.spawn(Vec2{}, 3.0));
    EXPECT_EQ(xp.collect(GameState::kPlaying, Vec2{kNaN, 0}, 10.0, 1.0), 0.0);
    EXPECT_EQ(xp.collect(GameState::kPlaying, Vec2{}, -1.0, 1.0), 0.0);
    EXPECT_EQ(xp.collect(GameState::kPlaying, Vec2{}, 10.0, kNaN), 0.0);
    EXPECT_EQ(xp.collect(GameState::kPlaying, Vec2{}, 10.0, 0.0), 0.0);
    EXPECT_EQ(xp.orbs().size(), 1u);
}

TEST(ExperienceOrbs, FullPoolMergesWithoutGrowthOrLoss) {
    ExperienceSystem xp;
    for (std::size_t i = 0; i < ExperienceSystem::kMaxOrbs; ++i) {
        ASSERT_TRUE(xp.spawn(Vec2{static_cast<double>(i) * 10.0, 0.0}, 1.0));
    }
    const double before = xp.total_xp();
    ASSERT_TRUE(xp.spawn(Vec2{12.0, 0.0}, 9.0)); // nearest is orb at x = 10
    EXPECT_EQ(xp.orbs().size(), ExperienceSystem::kMaxOrbs);
    EXPECT_DOUBLE_EQ(xp.total_xp(), before + 9.0);
    EXPECT_DOUBLE_EQ(xp.orbs()[1].xp, 10.0);
}

TEST(LevelUp, ThresholdStartsAtTenAndGrowsModestly) {
    EXPECT_DOUBLE_EQ(LevelUpSystem::threshold_for(1), 10.0);
    EXPECT_GT(LevelUpSystem::threshold_for(2), 10.0);
    EXPECT_LT(LevelUpSystem::threshold_for(2), 20.0);
    EXPECT_TRUE(std::isfinite(LevelUpSystem::threshold_for(LevelUpSystem::kMaxLevel)));
}

TEST(LevelUp, OnePickupQueuesEveryLevelAndKeepsSurplus) {
    LevelUpSystem lv;
    // 10 (L1->2) + 15 (L2->3) + 20 (L3->4) = 45, surplus 3.
    EXPECT_EQ(lv.add_xp(48.0), 3);
    EXPECT_EQ(lv.level(), 4);
    EXPECT_EQ(lv.pending_levels(), 3);
    EXPECT_DOUBLE_EQ(lv.xp(), 3.0);
    EXPECT_TRUE(lv.consume_pending());
    EXPECT_TRUE(lv.consume_pending());
    EXPECT_TRUE(lv.consume_pending());
    EXPECT_FALSE(lv.consume_pending());
}

TEST(LevelUp, InvalidAndHugeGrantsAreBounded) {
    LevelUpSystem lv;
    EXPECT_EQ(lv.add_xp(-1.0), -1);
    EXPECT_EQ(lv.add_xp(kNaN), -1);
    EXPECT_EQ(lv.add_xp(kInf), -1);
    EXPECT_EQ(lv.level(), 1);
    EXPECT_EQ(lv.pending_levels(), 0);
    EXPECT_EQ(lv.add_xp(1e300), LevelUpSystem::kMaxLevel - 1);
    EXPECT_EQ(lv.level(), LevelUpSystem::kMaxLevel);
    EXPECT_TRUE(lv.at_max_level());
    EXPECT_EQ(lv.pending_levels(), LevelUpSystem::kMaxLevel - 1);
    EXPECT_EQ(lv.add_xp(1e300), 0); // max level: surplus discarded
    EXPECT_EQ(lv.xp(), 0.0);
    EXPECT_EQ(lv.pending_levels(), LevelUpSystem::kMaxLevel - 1);
}

TEST(Inventory, EnforcesCapacityUniquenessAndLevels) {
    WeaponInventory inv;
    EXPECT_FALSE(inv.add("Bad Id", 1, 8));
    EXPECT_FALSE(inv.add("w0", 0, 8));
    EXPECT_FALSE(inv.add("w0", 9, 8));
    EXPECT_FALSE(inv.add("w0", 1, 0));
    EXPECT_FALSE(inv.add("w0", 1, WeaponRegistry::kMaxWeaponLevel + 1));
    ASSERT_TRUE(inv.add("bubble_shot", 1, 8));
    EXPECT_FALSE(inv.add("bubble_shot", 1, 8));
    for (int i = 1; i < 6; ++i) {
        ASSERT_TRUE(inv.add("w" + std::to_string(i), 1, 2));
    }
    EXPECT_TRUE(inv.full());
    EXPECT_FALSE(inv.add("w7", 1, 2));
    EXPECT_EQ(inv.entries().size(), 6u);
    EXPECT_TRUE(inv.upgrade("w1"));
    EXPECT_FALSE(inv.upgrade("w1")); // maxed
    EXPECT_FALSE(inv.upgrade("absent"));
    EXPECT_EQ(inv.level("w1"), 2);
}

TEST(Cards, OffersAreUniqueWeightedAndReproducible) {
    const PlayerRuntimeStats stats = PlayerRuntimeStats::from_character(fish());
    WeaponInventory inv;
    inv.add("bubble_shot", 1, 8);
    UpgradeCardSystem a(WeaponRegistry::make_default(), 1234);
    UpgradeCardSystem b(WeaponRegistry::make_default(), 1234);
    for (int i = 0; i < 20; ++i) {
        const UpgradeOffer oa = a.generate(stats, inv);
        const UpgradeOffer ob = b.generate(stats, inv);
        EXPECT_EQ(oa.card_ids, ob.card_ids);
        EXPECT_EQ(oa.generation, static_cast<std::uint64_t>(i + 1));
        ASSERT_EQ(oa.card_ids.size(), 3u);
        EXPECT_EQ(std::set<std::string>(oa.card_ids.begin(), oa.card_ids.end()).size(),
                  3u);
        for (const std::string &id : oa.card_ids) {
            ASSERT_NE(a.find(id), nullptr);
            EXPECT_TRUE(a.is_available(*a.find(id), stats, inv));
        }
    }
}

TEST(Cards, WeightsBiasTheDraw) {
    const PlayerRuntimeStats stats = PlayerRuntimeStats::from_character(fish());
    WeaponInventory inv;
    UpgradeCardSystem sys(WeaponRegistry::make_default(), 7, false);
    CardDefinition heavy = attr_card("heavy", StatKind::kArmor, 1.0);
    heavy.weight = 1000.0;
    ASSERT_TRUE(sys.add_card(heavy).empty());
    ASSERT_TRUE(sys.add_card(attr_card("light_a", StatKind::kDamage, 0.1)).empty());
    ASSERT_TRUE(sys.add_card(attr_card("light_b", StatKind::kCrit, 0.01)).empty());
    int heavy_first = 0;
    for (int i = 0; i < 200; ++i) {
        if (sys.generate(stats, inv).card_ids.front() == "heavy") {
            ++heavy_first;
        }
    }
    EXPECT_GT(heavy_first, 180);
    EXPECT_FALSE(sys.add_card(heavy).empty()); // duplicate id
    CardDefinition bad = attr_card("bad", StatKind::kArmor, 1.0);
    bad.weight = kNaN;
    EXPECT_FALSE(sys.add_card(bad).empty());
}

TEST(Cards, PoolSizesZeroOneTwoAreSafe) {
    const PlayerRuntimeStats stats = PlayerRuntimeStats::from_character(fish());
    WeaponInventory inv;
    UpgradeCardSystem sys(WeaponRegistry::make_default(), 1, false);
    UpgradeOffer o = sys.generate(stats, inv);
    EXPECT_TRUE(o.card_ids.empty());
    EXPECT_FALSE(sys.has_active_offer());
    PlayerRuntimeStats s = stats;
    EXPECT_EQ(sys.choose(o.generation, "x", s, inv), ChooseResult::kNoOffer);

    ASSERT_TRUE(sys.add_card(attr_card("one", StatKind::kArmor, 1.0)).empty());
    EXPECT_EQ(sys.generate(stats, inv).card_ids.size(), 1u);
    ASSERT_TRUE(sys.add_card(attr_card("two", StatKind::kDamage, 0.1)).empty());
    EXPECT_EQ(sys.generate(stats, inv).card_ids.size(), 2u);
}

TEST(Cards, EmptyPoolResolvesPendingLevelsWithoutRewards) {
    CharacterDefinition c = fish();
    ProgressionSession session(c, WeaponRegistry::make_default(), 9);
    ASSERT_TRUE(session.init_error().empty());
    // Exhaust every card: 7 attribute cards x10 ranks + 7 bubble upgrades.
    int guard = 0;
    while (guard++ < 1000) {
        ASSERT_EQ(session.grant_xp(GameState::kPlaying, 10.0 + 5.0 * (session.levels().level() - 1)) >= 0, true);
        if (!session.advance_offer()) {
            break;
        }
        const UpgradeOffer o = session.cards().current_offer();
        ASSERT_EQ(session.choose(o.generation, o.card_ids.front()),
                  ChooseResult::kApplied);
        while (session.advance_offer()) {
            const UpgradeOffer n = session.cards().current_offer();
            ASSERT_EQ(session.choose(n.generation, n.card_ids.front()),
                      ChooseResult::kApplied);
        }
        if (session.cards().available(session.stats(), session.inventory()).empty()) {
            break;
        }
    }
    ASSERT_TRUE(session.cards().available(session.stats(), session.inventory()).empty());
    const PlayerRuntimeStats frozen = session.stats();
    const int bubble = session.inventory().level("bubble_shot");
    EXPECT_EQ(bubble, 8);
    EXPECT_EQ(session.grant_xp(GameState::kPlaying, 1000.0) > 0, true);
    const int pending = session.levels().pending_levels();
    EXPECT_GT(pending, 0);
    EXPECT_FALSE(session.advance_offer()); // all resolved, no fake rewards
    EXPECT_EQ(session.levels().pending_levels(), 0);
    EXPECT_EQ(session.stats().max_hp, frozen.max_hp);
    EXPECT_EQ(session.inventory().level("bubble_shot"), bubble);
}

TEST(Cards, FullInventoryFiltersSeventhAcquireAndMaxedUpgrade) {
    const PlayerRuntimeStats stats = PlayerRuntimeStats::from_character(fish());
    // bubble_shot + 6 extras = 7 implemented weapons.
    UpgradeCardSystem sys(registry_with_extras(6), 3);
    WeaponInventory inv;
    ASSERT_TRUE(inv.add("bubble_shot", 1, 8));
    const CardDefinition *acquire = sys.find("weapon_acquire_extra_0");
    const CardDefinition *upgrade = sys.find("weapon_upgrade_bubble_shot");
    ASSERT_NE(acquire, nullptr);
    ASSERT_NE(upgrade, nullptr);
    EXPECT_FALSE(sys.is_available(*sys.find("weapon_acquire_bubble_shot"), stats, inv));
    EXPECT_TRUE(sys.is_available(*acquire, stats, inv));
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(inv.add("extra_" + std::to_string(i), 1, 3));
    }
    ASSERT_TRUE(inv.full());
    EXPECT_FALSE(sys.is_available(*sys.find("weapon_acquire_extra_5"), stats, inv));
    for (const CardDefinition *c : sys.available(stats, inv)) {
        EXPECT_NE(c->kind, CardKind::kWeaponAcquire);
    }
    while (inv.upgrade("bubble_shot")) {
    }
    EXPECT_FALSE(sys.is_available(*upgrade, stats, inv));
}

TEST(Cards, DefaultSceneNeverOffersUnsupportedWeapons) {
    WeaponRegistry registry = WeaponRegistry::make_default();
    WeaponDefinition future;
    future.id = "future_orbit";
    future.name = "Future";
    future.max_level = 5;
    future.implemented = false;
    ASSERT_TRUE(registry.add(future).empty());
    UpgradeCardSystem sys(registry, 5);
    EXPECT_EQ(sys.find("weapon_acquire_future_orbit"), nullptr);
    EXPECT_EQ(sys.find("weapon_upgrade_future_orbit"), nullptr);
    // Equipped bubble is filtered; only genuinely implemented, unowned
    // weapons may be acquired. The registry now has three real attacks.
    ProgressionSession session(fish(), registry, 5);
    for (const CardDefinition *c :
         session.cards().available(session.stats(), session.inventory())) {
        if (c->kind == CardKind::kWeaponAcquire) {
            const WeaponDefinition *w = registry.find(c->weapon_id);
            ASSERT_NE(w, nullptr);
            EXPECT_TRUE(w->implemented);
            EXPECT_FALSE(session.inventory().has(c->weapon_id));
        }
    }
}

TEST(Cards, StaleRepeatedAndInvalidChoicesDoNotMutate) {
    ProgressionSession session(fish(), WeaponRegistry::make_default(), 11);
    ASSERT_EQ(session.grant_xp(GameState::kPlaying, 10.0), 1);
    ASSERT_TRUE(session.advance_offer());
    const UpgradeOffer first = session.cards().current_offer();
    const PlayerRuntimeStats before = session.stats();

    EXPECT_EQ(session.choose(first.generation + 1, first.card_ids[0]),
              ChooseResult::kStaleGeneration);
    EXPECT_EQ(session.choose(first.generation, "not_a_card"),
              ChooseResult::kNotOffered);
    EXPECT_EQ(session.levels().pending_levels(), 1);
    EXPECT_EQ(session.stats().max_hp, before.max_hp);

    ASSERT_EQ(session.choose(first.generation, first.card_ids[0]),
              ChooseResult::kApplied);
    const PlayerRuntimeStats after = session.stats();
    // Repeated callback for the same offer is rejected and changes nothing.
    EXPECT_EQ(session.choose(first.generation, first.card_ids[0]),
              ChooseResult::kNoOffer);
    EXPECT_EQ(session.stats().armor, after.armor);
    EXPECT_EQ(session.stats().max_hp, after.max_hp);
    EXPECT_EQ(session.levels().pending_levels(), 0);
}

TEST(Cards, StaleGenerationAfterNewOfferIsRejected) {
    ProgressionSession session(fish(), WeaponRegistry::make_default(), 13);
    ASSERT_EQ(session.grant_xp(GameState::kPlaying, 25.0), 2);
    ASSERT_TRUE(session.advance_offer());
    const UpgradeOffer first = session.cards().current_offer();
    ASSERT_EQ(session.choose(first.generation, first.card_ids[0]),
              ChooseResult::kApplied);
    ASSERT_TRUE(session.advance_offer()); // second queued level, new offer
    const UpgradeOffer second = session.cards().current_offer();
    EXPECT_NE(second.generation, first.generation);
    const int pending = session.levels().pending_levels();
    EXPECT_EQ(session.choose(first.generation, second.card_ids[0]),
              ChooseResult::kStaleGeneration);
    EXPECT_EQ(session.levels().pending_levels(), pending);
}

TEST(Cards, ApplyingEveryAttributeCardChangesStatsAndRespectsCaps) {
    const PlayerRuntimeStats base = PlayerRuntimeStats::from_character(fish());
    WeaponInventory inv;
    inv.add("bubble_shot", 1, 8);
    UpgradeCardSystem sys(WeaponRegistry::make_default(), 21);
    PlayerRuntimeStats s = base;
    const struct {
        const char *id;
        double PlayerRuntimeStats::*field;
        double delta;
    } expected[] = {
        {"attr_max_hp", &PlayerRuntimeStats::max_hp, 20.0},
        {"attr_damage", &PlayerRuntimeStats::damage_multiplier, 0.10},
        {"attr_attack_speed", &PlayerRuntimeStats::attack_speed_multiplier, 0.08},
        {"attr_move_speed", &PlayerRuntimeStats::move_speed, 0.08 * 200.0},
        {"attr_armor", &PlayerRuntimeStats::armor, 2.0},
        {"attr_crit", &PlayerRuntimeStats::crit_chance, 0.03},
        {"attr_pickup", &PlayerRuntimeStats::pickup_radius, 15.0},
    };
    for (const auto &e : expected) {
        // Offer only this card by exhausting draws until it appears.
        UpgradeOffer o;
        bool found = false;
        for (int i = 0; i < 200 && !found; ++i) {
            o = sys.generate(s, inv);
            found = std::find(o.card_ids.begin(), o.card_ids.end(), e.id) !=
                    o.card_ids.end();
        }
        ASSERT_TRUE(found) << e.id;
        const double before = s.*(e.field);
        ASSERT_EQ(sys.choose(o.generation, e.id, s, inv), ChooseResult::kApplied);
        EXPECT_NEAR(s.*(e.field) - before, e.delta, 1e-9) << e.id;
        EXPECT_EQ(sys.rank(e.id), 1);
    }
    // The immutable base copy is untouched; cards only change runtime stats.
    EXPECT_EQ(base.max_hp, 100.0);
}

TEST(Cards, RankCapAndStatCapRemoveCardsFromPool) {
    WeaponInventory inv;
    UpgradeCardSystem sys(WeaponRegistry::make_default(), 1, false);
    ASSERT_TRUE(sys.add_card(attr_card("armor", StatKind::kArmor, 2.0, 2)).empty());
    ASSERT_TRUE(sys.add_card(attr_card("crit", StatKind::kCrit, 0.5, 10)).empty());
    PlayerRuntimeStats s = PlayerRuntimeStats::from_character(fish());
    for (int i = 0; i < 2; ++i) {
        const UpgradeOffer o = sys.generate(s, inv);
        ASSERT_EQ(sys.choose(o.generation, "armor", s, inv) == ChooseResult::kApplied ||
                      sys.choose(o.generation, "crit", s, inv) == ChooseResult::kApplied,
                  true);
    }
    // Drive crit to its cap; once there it must no longer be offered.
    s.crit_chance = PlayerRuntimeStats::kMaxCrit;
    s.armor = 0.0;
    for (const CardDefinition *c : sys.available(s, inv)) {
        EXPECT_NE(c->stat, StatKind::kCrit);
    }
    s.crit_chance = 0.9;
    const CardDefinition *crit = sys.find("crit");
    ASSERT_NE(crit, nullptr);
    const UpgradeOffer o = sys.generate(s, inv);
    if (std::find(o.card_ids.begin(), o.card_ids.end(), "crit") != o.card_ids.end()) {
        ASSERT_EQ(sys.choose(o.generation, "crit", s, inv), ChooseResult::kApplied);
        EXPECT_DOUBLE_EQ(s.crit_chance, PlayerRuntimeStats::kMaxCrit); // clamped, still a change
    }
}

TEST(Session, BubbleUpgradeIncreasesWorldDamagePerLevel) {
    ProgressionSession session(fish(), WeaponRegistry::make_default(), 2);
    EXPECT_DOUBLE_EQ(session.weapon_damage_scale(), 1.0);
    CombatConfig cfg;
    cfg.waves_enabled = false;
    CombatWorld world(session.stats().combat(), 1, cfg);
    const double base = world.weapon_damage();
    PlayerRuntimeStats s = session.stats();
    WeaponInventory inv;
    inv.add("bubble_shot", 1, 8);
    ASSERT_TRUE(inv.upgrade("bubble_shot"));
    ASSERT_TRUE(world.update_player_stats(s.combat(), 1.0 + 0.2));
    EXPECT_GT(world.weapon_damage(), base);
    EXPECT_NEAR(world.weapon_damage(), base * 1.2, 1e-9);
    // Absolute (not compounding): a second call with the same scale is stable.
    ASSERT_TRUE(world.update_player_stats(s.combat(), 1.2));
    EXPECT_NEAR(world.weapon_damage(), base * 1.2, 1e-9);
}

TEST(CombatWorldStats, HpCardHealsAddedOnlyAndOthersDoNotRefill) {
    CombatConfig cfg;
    cfg.waves_enabled = false;
    CombatStats stats;
    stats.max_hp = 100.0;
    GameStateManager states;
    states.start_run();
    CombatWorld world(stats, 1, cfg);
    EnemySpawn hit;
    hit.position = Vec2{};
    hit.speed = 0.0;
    hit.contact_damage = 30.0;
    ASSERT_TRUE(world.spawn_enemy(hit));
    world.tick(states, 1.0 / 60.0, Vec2{});
    const double wounded = world.hp();
    ASSERT_LT(wounded, 100.0);

    CombatStats next = stats;
    next.armor = 2.0;
    next.damage_multiplier = 1.1;
    ASSERT_TRUE(world.update_player_stats(next, 1.0));
    EXPECT_DOUBLE_EQ(world.hp(), wounded); // no refill

    next.max_hp = 120.0;
    ASSERT_TRUE(world.update_player_stats(next, 1.0));
    EXPECT_DOUBLE_EQ(world.max_hp(), 120.0);
    EXPECT_DOUBLE_EQ(world.hp(), wounded + 20.0);
    EXPECT_EQ(world.enemies().size(), 1u); // world not rebuilt
}

TEST(CombatWorldStats, RejectsNonFiniteAndKeepsCooldownFraction) {
    CombatConfig cfg;
    cfg.waves_enabled = false;
    CombatStats stats;
    GameStateManager states;
    states.start_run();
    CombatWorld world(stats, 1, cfg);
    EnemySpawn target;
    target.position = Vec2{200.0, 0.0};
    target.speed = 0.0;
    target.hp = 1e6;
    ASSERT_TRUE(world.spawn_enemy(target));
    world.tick(states, 1.0 / 60.0, Vec2{}); // fires, starts cooldown
    world.tick(states, 0.2, Vec2{});
    const double fraction = world.weapon_cooldown() / world.weapon_interval();
    ASSERT_GT(fraction, 0.0);
    ASSERT_LT(fraction, 1.0);

    CombatStats bad = stats;
    bad.armor = kNaN;
    EXPECT_FALSE(world.update_player_stats(bad, 1.0));
    EXPECT_FALSE(world.update_player_stats(stats, 0.0));
    EXPECT_FALSE(world.update_player_stats(stats, kInf));

    CombatStats faster = stats;
    faster.attack_speed_multiplier = 1.5;
    ASSERT_TRUE(world.update_player_stats(faster, 1.0));
    EXPECT_LT(world.weapon_interval(), cfg.weapon_base_interval);
    EXPECT_NEAR(world.weapon_cooldown() / world.weapon_interval(), fraction, 1e-9);
}

TEST(Session, XpMultiplierAndPickupFeedLevelsOnlyWhilePlaying) {
    CharacterDefinition c = fish();
    c.xp_multiplier = 2.0;
    ProgressionSession session(c, WeaponRegistry::make_default(), 4);
    std::vector<KillEvent> kills{{Vec2{50.0, 0.0}, 5.0, 1}}; // inside 80
    EXPECT_EQ(session.ingest_and_collect(GameState::kPaused, kills, Vec2{}), 0);
    EXPECT_EQ(session.ingest_and_collect(GameState::kUpgrade, kills, Vec2{}), 0);
    EXPECT_TRUE(session.experience().orbs().empty());
    EXPECT_EQ(session.ingest_and_collect(GameState::kPlaying, kills, Vec2{}), 1);
    EXPECT_EQ(session.levels().level(), 2);
    EXPECT_TRUE(session.needs_upgrade());
    // Far orb is left for later.
    std::vector<KillEvent> far{{Vec2{400.0, 0.0}, 5.0, 2}};
    EXPECT_EQ(session.ingest_and_collect(GameState::kPlaying, far, Vec2{}), 0);
    EXPECT_EQ(session.experience().orbs().size(), 1u);
}

TEST(Session, FrozenTimerWhilePendingChoices) {
    GameStateManager states;
    GameTimer timer(states);
    ASSERT_TRUE(states.start_run());
    ProgressionSession session(fish(), WeaponRegistry::make_default(), 6);
    EXPECT_EQ(timer.advance(1.0), GameTimer::AdvanceResult::kAdvanced);
    ASSERT_EQ(session.grant_xp(states.state(), 25.0), 2);
    ASSERT_TRUE(states.begin_upgrade());
    for (int i = 0; i < 2; ++i) {
        ASSERT_TRUE(session.advance_offer());
        EXPECT_EQ(states.state(), GameState::kUpgrade); // stays Upgrade
        EXPECT_EQ(timer.advance(5.0), GameTimer::AdvanceResult::kFrozen);
        const UpgradeOffer o = session.cards().current_offer();
        ASSERT_EQ(session.choose(o.generation, o.card_ids[0]), ChooseResult::kApplied);
    }
    EXPECT_FALSE(session.advance_offer());
    EXPECT_DOUBLE_EQ(timer.elapsed_seconds(), 1.0);
    ASSERT_TRUE(states.complete_upgrade());
    EXPECT_EQ(timer.advance(1.0), GameTimer::AdvanceResult::kAdvanced);
    // Terminal state grants nothing.
    ASSERT_TRUE(states.lose());
    EXPECT_EQ(session.grant_xp(states.state(), 100.0), 0);
}

TEST(Session, UnimplementedInitialWeaponIsAnError) {
    CharacterDefinition c = fish();
    c.initial_weapon_id = "ghost";
    ProgressionSession session(c, WeaponRegistry::make_default(), 1);
    EXPECT_FALSE(session.init_error().empty());
}

TEST(Cards, VeryLargeFiniteWeightsDoNotOverflowIntoLastEntryBias) {
    std::set<std::string> seen;
    const auto stats = PlayerRuntimeStats::from_character(fish());
    WeaponInventory inventory;
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        UpgradeCardSystem cards(WeaponRegistry::make_default(), seed, false);
        for (int i = 0; i < 6; ++i) {
            CardDefinition card;
            card.id = "large_" + std::to_string(i);
            card.name = "Large weight";
            card.description = "HP +1";
            card.stat = StatKind::kMaxHp;
            card.amount = 1.0;
            card.weight = 1e308;
            ASSERT_TRUE(cards.add_card(card).empty());
        }
        const auto offer = cards.generate(stats, inventory);
        ASSERT_EQ(offer.card_ids.size(), 3u);
        seen.insert(offer.card_ids.begin(), offer.card_ids.end());
    }
    EXPECT_EQ(seen.size(), 6u);
}

TEST(Cards, RuntimeStatsMatchCombatCapsBeforeRewardsAreOffered) {
    auto c = fish();
    c.attack_speed_multiplier = 0.01;
    c.damage_multiplier = 100.0;
    c.max_hp = 0.5;
    auto stats = PlayerRuntimeStats::from_character(c);
    EXPECT_DOUBLE_EQ(stats.attack_speed_multiplier, 0.1);
    EXPECT_DOUBLE_EQ(stats.damage_multiplier, 10.0);
    EXPECT_DOUBLE_EQ(stats.max_hp, 1.0);
    UpgradeCardSystem cards(WeaponRegistry::make_default(), 1, false);
    CardDefinition card;
    card.id = "speed"; card.name = "Speed";
    card.stat = StatKind::kAttackSpeed; card.amount = 0.08;
    ASSERT_TRUE(cards.add_card(card).empty());
    WeaponInventory inventory;
    auto offer = cards.generate(stats, inventory);
    WeaponManager weapon;
    weapon.configure(stats.combat(), CombatConfig());
    const double before = weapon.interval();
    ASSERT_EQ(cards.choose(offer.generation, "speed", stats, inventory), ChooseResult::kApplied);
    weapon.configure(stats.combat(), CombatConfig());
    EXPECT_LT(weapon.interval(), before);
}

TEST(Cards, FiniteExtremeBaseSpeedKeepsCardCapFinite) {
    auto c = fish();
    c.move_speed = std::numeric_limits<double>::max();
    auto stats = PlayerRuntimeStats::from_character(c);
    EXPECT_TRUE(std::isfinite(stats.base_move_speed * PlayerRuntimeStats::kMaxMoveSpeedFactor));
}
