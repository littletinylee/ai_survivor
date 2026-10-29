#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "core/combat.h"
#include "core/player_controller.h"
#include "core/progression.h"
#include "core/run_simulation.h"
#include "core/weapon.h"

// Controlled scenarios for KiteBot and KiteBot::rank_cards. Each one places a
// small hand-built situation and checks a single decision. These prove the
// bot's local preferences, not that the default fish wins or loses.

namespace {

using namespace ai_survivors;

CharacterDefinition fish() {
    CharacterDefinition c;
    c.id = "deepseek_big_fish";
    c.name = "fish";
    c.description = "default fish values";
    c.max_hp = 100.0;
    c.move_speed = 220.0;
    c.crit_chance = 0.05;
    c.pickup_radius = 80.0;
    c.initial_weapon_id = "bubble_shot";
    c.passive_id = "deep_dive";
    c.passive_description = "placeholder";
    return c;
}

// Walls on, waves off: the test places every enemy itself.
struct Scene {
    WeaponRegistry weapons = WeaponRegistry::make_default();
    CharacterDefinition character = fish();
    ProgressionSession progression{character, weapons, 7};
    CombatWorld world{CombatStats::from_character(character), 3, quiet()};

    static CombatConfig quiet() {
        CombatConfig cfg;
        cfg.waves_enabled = false;
        return cfg;
    }

    void add_enemy(Vec2 position, double speed) {
        EnemySpawn spec;
        spec.position = position;
        spec.speed = speed;
        ASSERT_TRUE(world.spawn_enemy(spec));
    }
};

// Id of the first card that satisfies pred, or empty when none exists.
template <typename Pred>
std::string find_card_id(const ProgressionSession &progression, Pred pred) {
    for (const CardDefinition &card : progression.cards().cards()) {
        if (pred(card)) {
            return card.id;
        }
    }
    return std::string();
}

double length(KiteBot::Input in) {
    return std::sqrt(in.x * in.x + in.y * in.y);
}

TEST(KiteBot, MovesAwayFromContactThreat) {
    Scene s;
    s.add_enemy(Vec2{80.0, 0.0}, 0.0); // stationary, 80 units east
    KiteBot bot;
    const KiteBot::Input in = bot.next_input(Vec2{0.0, 0.0}, s.world, s.progression);
    EXPECT_LT(in.x, 0.0);
    EXPECT_NEAR(length(in), 1.0, 1e-9);
}

TEST(KiteBot, WallPinnedBotSlidesAlongWallInsteadOfStopping) {
    Scene s;
    const double wall_x = s.world.config().arena_half_width -
                          s.world.config().player_radius;
    s.add_enemy(Vec2{wall_x - 100.0, 0.0}, 0.0); // threat straight inward
    KiteBot bot;
    const KiteBot::Input in =
        bot.next_input(Vec2{wall_x, 0.0}, s.world, s.progression);
    EXPECT_GT(length(in), 0.5) << "stop is not a safe tangent here";
    EXPECT_GE(in.x, -1e-9) << "must not steer into the threat";
    EXPECT_GT(std::fabs(in.y), 0.9) << "expected to slide along the wall";
}

TEST(KiteBot, GoesTowardSafeOrbWhenNoThreatIsNear) {
    Scene s;
    ASSERT_TRUE(s.progression.experience().spawn(Vec2{200.0, 0.0}, 1.0));
    KiteBot bot;
    const KiteBot::Input in = bot.next_input(Vec2{0.0, 0.0}, s.world, s.progression);
    EXPECT_GT(in.x, 0.9);
}

TEST(KiteBot, CachesDecisionForSixFramesThenReplans) {
    Scene s;
    s.progression.experience().spawn(Vec2{200.0, 0.0}, 1.0);
    KiteBot bot;
    const KiteBot::Input first =
        bot.next_input(Vec2{0.0, 0.0}, s.world, s.progression);
    // Player position changes, but the cached decision holds for 6 calls.
    for (int i = 1; i < KiteBot::kDecisionFrames; ++i) {
        const KiteBot::Input again =
            bot.next_input(Vec2{0.0, 100.0}, s.world, s.progression);
        EXPECT_EQ(again.x, first.x);
        EXPECT_EQ(again.y, first.y);
    }
    // The 7th call re-plans from (0, 100), where the orb lies south-east.
    const KiteBot::Input replanned =
        bot.next_input(Vec2{0.0, 100.0}, s.world, s.progression);
    EXPECT_LT(replanned.y, 0.0);
}

TEST(KiteBot, PrefersNewWeaponOverOtherCards) {
    Scene s;
    const std::string acquire = find_card_id(
        s.progression, [&](const CardDefinition &card) {
            const WeaponDefinition *w = s.progression.weapons().find(card.weapon_id);
            return card.kind == CardKind::kWeaponAcquire && w != nullptr &&
                   w->implemented && !s.progression.inventory().has(card.weapon_id);
        });
    const std::string armor = find_card_id(
        s.progression, [](const CardDefinition &card) {
            return card.stat == StatKind::kArmor;
        });
    ASSERT_FALSE(acquire.empty());
    ASSERT_FALSE(armor.empty());
    UpgradeOffer offer;
    offer.card_ids = {armor, acquire};
    const std::vector<std::string> order =
        KiteBot::rank_cards(offer, s.progression, 1.0);
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], acquire);
}

TEST(KiteBot, PrefersMaxHpWhenInjuredOnly) {
    Scene s;
    const std::string max_hp = find_card_id(
        s.progression, [](const CardDefinition &card) {
            return card.stat == StatKind::kMaxHp;
        });
    const std::string pickup = find_card_id(
        s.progression, [](const CardDefinition &card) {
            return card.stat == StatKind::kPickupRadius;
        });
    ASSERT_FALSE(max_hp.empty());
    ASSERT_FALSE(pickup.empty());
    UpgradeOffer offer;
    offer.card_ids = {pickup, max_hp};

    EXPECT_EQ(KiteBot::rank_cards(offer, s.progression, 0.3)[0], max_hp);
    EXPECT_EQ(KiteBot::rank_cards(offer, s.progression, 1.0)[0], pickup);
}

TEST(KiteBot, UnknownCardIdsSortLastButStayInOrder) {
    Scene s;
    const std::string armor = find_card_id(
        s.progression, [](const CardDefinition &card) {
            return card.stat == StatKind::kArmor;
        });
    ASSERT_FALSE(armor.empty());
    UpgradeOffer offer;
    offer.card_ids = {"future_card_not_in_registry", armor};
    const std::vector<std::string> order =
        KiteBot::rank_cards(offer, s.progression, 1.0);
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], armor);
    EXPECT_EQ(order[1], "future_card_not_in_registry");
}

} // namespace
