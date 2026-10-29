#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/combat.h"
#include "core/progression.h"
#include "core/weapon.h"

using namespace ai_survivors;

namespace {

constexpr double kStep = 1.0 / 60.0;

CombatConfig manual_config() {
    CombatConfig c;
    c.waves_enabled = false;
    return c;
}

EnemySpawn enemy_at(double x, double y, double hp = 100.0) {
    EnemySpawn s;
    s.position = Vec2{x, y};
    s.hp = hp;
    s.speed = 0.0;
    s.contact_damage = 1.0;
    s.xp_value = 3.0;
    return s;
}

std::vector<WeaponSpec> loadout_of(const WeaponRegistry &registry,
                                   std::initializer_list<std::string> ids) {
    WeaponInventory inv;
    for (const std::string &id : ids) {
        const WeaponDefinition *w = registry.find(id);
        EXPECT_NE(w, nullptr) << id;
        EXPECT_TRUE(inv.add(id, 1, w->max_level));
    }
    std::vector<WeaponSpec> specs;
    EXPECT_EQ(compile_loadout(registry, inv, specs), "");
    return specs;
}

struct Fixture {
    GameStateManager states;
    CombatWorld world;
    explicit Fixture(const std::vector<WeaponSpec> &specs)
        : world(CombatStats(), 7, manual_config()) {
        states.start_run();
        EXPECT_TRUE(world.set_loadout(specs));
    }
    void run(double seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kStep + 0.5); ++i) {
            world.tick(states, kStep, Vec2{0.0, 0.0});
        }
    }
};

const WeaponRegistry kRegistry = WeaponRegistry::make_default();

} // namespace

TEST(Weapons, DefaultRegistryHasThreeImplementedKinds) {
    ASSERT_EQ(kRegistry.weapons().size(), 3u);
    EXPECT_EQ(kRegistry.find("bubble_shot")->attack_kind, AttackKind::kProjectile);
    EXPECT_EQ(kRegistry.find("tail_sweep")->attack_kind, AttackKind::kBurst);
    EXPECT_EQ(kRegistry.find("data_pool")->attack_kind, AttackKind::kZone);
    for (const WeaponDefinition &w : kRegistry.weapons()) {
        EXPECT_TRUE(w.implemented);
    }
}

TEST(Weapons, RegistryRejectsOutOfBoundsDefinitions) {
    WeaponRegistry r;
    WeaponDefinition w;
    w.id = "bad_zone";
    w.name = "x";
    w.attack_kind = AttackKind::kZone; // no radius/lifetime/tick
    EXPECT_NE(r.add(w), "");
    w.id = "bad_count";
    w.attack_kind = AttackKind::kProjectile;
    w.projectile_count = 99;
    EXPECT_NE(r.add(w), "");
    w.projectile_count = 1;
    w.base_damage = WeaponLimits::kMaxDamage;
    w.damage_per_level = 0.2;
    EXPECT_NE(r.add(w), ""); // every offered level must compile successfully
    EXPECT_TRUE(r.weapons().empty());
}

TEST(Weapons, BurstHitsEveryInRangeEnemyOnceAndKnocksBack) {
    Fixture f(loadout_of(kRegistry, {"tail_sweep"}));
    f.world.spawn_enemy(enemy_at(100.0, 0.0));
    f.world.spawn_enemy(enemy_at(0.0, -100.0));
    f.world.spawn_enemy(enemy_at(400.0, 0.0)); // out of range
    f.run(1.0);                                 // shorter than the 1.6 s cooldown
    ASSERT_EQ(f.world.enemies().size(), 3u);
    EXPECT_DOUBLE_EQ(f.world.enemies()[0].hp, 90.0); // exactly one activation
    EXPECT_DOUBLE_EQ(f.world.enemies()[1].hp, 90.0);
    EXPECT_DOUBLE_EQ(f.world.enemies()[2].hp, 100.0);
    EXPECT_NEAR(f.world.enemies()[0].position.x, 140.0, 1e-9);
    EXPECT_NEAR(f.world.enemies()[2].position.x, 400.0, 1e-9);
}

TEST(Weapons, BurstAndZoneSpendNothingWithoutTarget) {
    Fixture f(loadout_of(kRegistry, {"tail_sweep", "data_pool"}));
    f.world.spawn_enemy(enemy_at(900.0, 0.0)); // beyond every range
    f.run(0.5);
    for (const WeaponManager::Slot &s : f.world.weapon_slots()) {
        EXPECT_DOUBLE_EQ(s.cooldown, 0.0) << s.spec.id;
    }
    EXPECT_TRUE(f.world.zones().empty());
    EXPECT_TRUE(f.world.bursts().empty());
}

TEST(Weapons, ZoneTicksPeriodicallyThenExpires) {
    Fixture f(loadout_of(kRegistry, {"data_pool"}));
    f.world.spawn_enemy(enemy_at(300.0, 0.0, 1000.0));
    f.run(0.3);
    ASSERT_EQ(f.world.zones().size(), 1u);
    EXPECT_DOUBLE_EQ(f.world.zones()[0].position.x, 300.0); // at the target
    EXPECT_DOUBLE_EQ(f.world.enemies()[0].hp, 1000.0);      // first tick at 0.5 s
    f.run(3.2);                                             // t = 3.5 s < 4 s cooldown
    EXPECT_TRUE(f.world.zones().empty());
    EXPECT_NEAR(f.world.enemies()[0].hp, 1000.0 - 6.0 * 6.0, 1e-6); // 6 ticks
}

TEST(Weapons, FrozenOutsidePlaying) {
    Fixture f(loadout_of(kRegistry, {"tail_sweep", "data_pool"}));
    f.world.spawn_enemy(enemy_at(100.0, 0.0));
    f.run(0.1);
    ASSERT_TRUE(f.states.begin_upgrade());
    const double hp = f.world.enemies()[0].hp;
    const double cd = f.world.weapon_slots()[0].cooldown;
    const std::size_t zones = f.world.zones().size();
    const double age = f.world.zones().empty() ? 0.0 : f.world.zones()[0].age;
    const double t = f.world.combat_time();
    for (int i = 0; i < 120; ++i) {
        EXPECT_EQ(f.world.tick(f.states, kStep, Vec2{0.0, 0.0}),
                  CombatWorld::TickResult::kFrozen);
    }
    EXPECT_DOUBLE_EQ(f.world.enemies()[0].hp, hp);
    EXPECT_DOUBLE_EQ(f.world.weapon_slots()[0].cooldown, cd);
    EXPECT_EQ(f.world.zones().size(), zones);
    EXPECT_DOUBLE_EQ(f.world.zones().empty() ? 0.0 : f.world.zones()[0].age, age);
    EXPECT_DOUBLE_EQ(f.world.combat_time(), t);
}

TEST(Weapons, InvalidLoadoutIsRejectedAtomically) {
    Fixture f(loadout_of(kRegistry, {"bubble_shot"}));
    const std::vector<WeaponSpec> good = loadout_of(kRegistry, {"tail_sweep"});
    std::vector<WeaponSpec> seven;
    for (int i = 0; i < 7; ++i) {
        WeaponSpec s = good[0];
        s.id = "w" + std::to_string(i);
        seven.push_back(s);
    }
    EXPECT_FALSE(f.world.set_loadout(seven));
    seven.pop_back();
    EXPECT_TRUE(f.world.set_loadout(seven)); // six unique weapons are fine
    EXPECT_EQ(f.world.weapon_slots().size(), 6u);

    std::vector<WeaponSpec> dup = {good[0], good[0]};
    EXPECT_FALSE(f.world.set_loadout(dup));
    std::vector<WeaponSpec> bad = {good[0]};
    bad[0].area_radius = -1.0;
    EXPECT_FALSE(f.world.set_loadout(bad));
    bad = good;
    bad[0].level = 0;
    EXPECT_FALSE(f.world.set_loadout(bad));
    EXPECT_FALSE(f.world.set_loadout({}));
    EXPECT_EQ(f.world.weapon_slots().size(), 6u); // untouched by every rejection

    WeaponInventory inv;
    for (int i = 0; i < 6; ++i) {
        EXPECT_TRUE(inv.add("w" + std::to_string(i), 1, 3));
    }
    EXPECT_FALSE(inv.add("w6", 1, 3));
}

TEST(Weapons, UpdateKeepsCooldownFractionAndNewWeaponStartsReady) {
    Fixture f(loadout_of(kRegistry, {"bubble_shot"}));
    f.world.spawn_enemy(enemy_at(300.0, 0.0, 1e6));
    f.run(0.3); // bubble fired and is cooling down
    const WeaponManager::Slot before = f.world.weapon_slots()[0];
    ASSERT_GT(before.cooldown, 0.0);
    const double fraction = before.cooldown / before.interval;
    const std::size_t enemies = f.world.enemies().size();
    const double hp = f.world.hp();

    WeaponInventory inv;
    inv.add("bubble_shot", 2, 8);
    inv.add("data_pool", 1, 8);
    std::vector<WeaponSpec> specs;
    ASSERT_EQ(compile_loadout(kRegistry, inv, specs), "");
    ASSERT_TRUE(f.world.set_loadout(specs));

    ASSERT_EQ(f.world.weapon_slots().size(), 2u);
    const WeaponManager::Slot &bubble = f.world.weapon_slots()[0];
    EXPECT_NEAR(bubble.cooldown / bubble.interval, fraction, 1e-9);
    EXPECT_DOUBLE_EQ(bubble.spec.damage, 12.0 * 1.2); // scaled once
    EXPECT_DOUBLE_EQ(f.world.weapon_slots()[1].cooldown, 0.0);
    EXPECT_EQ(f.world.enemies().size(), enemies);
    EXPECT_DOUBLE_EQ(f.world.hp(), hp);
}

TEST(Weapons, SimultaneousHitsKillOnce) {
    Fixture f(loadout_of(kRegistry, {"bubble_shot", "tail_sweep", "data_pool"}));
    f.world.spawn_enemy(enemy_at(90.0, 0.0, 1.0));
    f.run(1.0);
    EXPECT_EQ(f.world.kill_count(), 1u);
    EXPECT_EQ(f.world.take_kill_events().size(), 1u);
    EXPECT_TRUE(f.world.enemies().empty());
}

TEST(Weapons, PiercingHitsEachEnemyOncePerProjectile) {
    WeaponRegistry r;
    WeaponDefinition w;
    w.id = "pierce_test";
    w.name = "pierce";
    w.implemented = true;
    w.base_damage = 10.0;
    w.cooldown = 100.0;
    w.pierce_count = 1;
    ASSERT_EQ(r.add(w), "");
    Fixture f(loadout_of(r, {"pierce_test"}));
    f.world.spawn_enemy(enemy_at(100.0, 0.0));
    f.world.spawn_enemy(enemy_at(160.0, 0.0));
    f.world.spawn_enemy(enemy_at(220.0, 0.0)); // third enemy exceeds pierce 1
    f.run(1.0);
    int hit_once = 0;
    for (const Enemy &e : f.world.enemies()) {
        EXPECT_TRUE(e.hp == 90.0 || e.hp == 100.0); // never hit twice
        hit_once += e.hp == 90.0 ? 1 : 0;
    }
    EXPECT_EQ(hit_once, 2);
    EXPECT_TRUE(f.world.projectiles().empty());
}

TEST(Weapons, AcquireCardChangesRealAttacks) {
    UpgradeCardSystem cards(kRegistry, 5);
    PlayerRuntimeStats stats;
    WeaponInventory inv;
    inv.add("bubble_shot", 1, 8);
    bool acquired = false;
    for (int i = 0; i < 200 && !acquired; ++i) {
        const UpgradeOffer offer = cards.generate(stats, inv);
        for (const std::string &id : offer.card_ids) {
            if (id == "weapon_acquire_tail_sweep") {
                acquired = cards.choose(offer.generation, id, stats, inv) ==
                           ChooseResult::kApplied;
            }
        }
    }
    ASSERT_TRUE(acquired);
    ASSERT_TRUE(inv.has("tail_sweep"));
    std::vector<WeaponSpec> specs;
    ASSERT_EQ(compile_loadout(kRegistry, inv, specs), "");
    Fixture f(specs);
    f.world.spawn_enemy(enemy_at(100.0, 0.0));
    f.run(0.1);
    EXPECT_LE(f.world.enemies()[0].hp, 90.0);
    EXPECT_FALSE(f.world.bursts().empty());
}

TEST(Weapons, UpgradedBurstAndZoneDealMoreActualDamage) {
    for (const std::string id : {"tail_sweep", "data_pool"}) {
        auto damage_at = [&](int level) {
            WeaponInventory inv;
            inv.add(id, level, 8);
            std::vector<WeaponSpec> specs;
            EXPECT_EQ(compile_loadout(kRegistry, inv, specs), "");
            Fixture f(specs);
            f.world.spawn_enemy(enemy_at(100.0, 0.0, 1000.0));
            f.run(id == "tail_sweep" ? kStep : 0.6);
            return 1000.0 - f.world.enemies()[0].hp;
        };
        const double base = damage_at(1);
        EXPECT_GT(base, 0.0);
        EXPECT_NEAR(damage_at(2), base * 1.2, 1e-8) << id;
    }
}

TEST(Weapons, ZonePoolRemainsBoundedUnderSixFastWeapons) {
    std::vector<WeaponSpec> specs = loadout_of(kRegistry, {"data_pool"});
    specs[0].cooldown = WeaponLimits::kMinCooldown;
    specs[0].area_lifetime = WeaponLimits::kMaxLifetime;
    specs[0].damage = 0.1;
    for (int i = 1; i < 6; ++i) {
        WeaponSpec extra = specs[0];
        extra.id = "zone_" + std::to_string(i);
        specs.push_back(extra);
    }
    Fixture f(specs);
    f.world.spawn_enemy(enemy_at(300.0, 0.0, 1e6));
    f.run(5.0);
    EXPECT_EQ(f.world.zones().size(), AreaSink::kMaxZones);
    EXPECT_EQ(f.world.weapon_slots().size(), 6u);
    EXPECT_EQ(f.world.enemies().size(), 1u);
}
