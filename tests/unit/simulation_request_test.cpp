#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/character.h"
#include "core/run_simulation.h"
#include "core/simulation_request.h"

// Pure parsing for --simulate-run options. These tests cover argument handling
// only; they do not tune or claim anything about gameplay balance.

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
    c.passive_id = "free_rice";
    c.passive_description = "mirrors the fish JSON";
    return c;
}

bool parse(const std::vector<std::string> &args, SimulationRequest &request,
           std::string &err) {
    request = SimulationRequest{};
    err.clear();
    return parse_simulation_args(args, request, err);
}

TEST(SimulationRequest, DefaultsWhenNoSimulationArgs) {
    SimulationRequest request;
    std::string err;
    ASSERT_TRUE(parse({}, request, err)) << err;
    EXPECT_FALSE(request.requested);
    EXPECT_EQ(request.seed, 1u);
    EXPECT_EQ(request.policy, SimulationPolicy::kKiteAndCollect);
    EXPECT_EQ(request.character_id, "deepseek_big_fish");
    EXPECT_TRUE(err.empty());
}

TEST(SimulationRequest, GameArgsOutsideSimulationAreNotRejected) {
    SimulationRequest request;
    std::string err;
    EXPECT_TRUE(parse({"--integration-test"}, request, err)) << err;
    EXPECT_FALSE(request.requested);
    EXPECT_TRUE(parse({"--visual-battle", "--unrelated"}, request, err)) << err;
    EXPECT_FALSE(request.requested);
}

TEST(SimulationRequest, DefaultsWithSimulateRunOnly) {
    SimulationRequest request;
    std::string err;
    ASSERT_TRUE(parse({"--simulate-run"}, request, err)) << err;
    EXPECT_TRUE(request.requested);
    EXPECT_EQ(request.seed, 1u);
    EXPECT_EQ(request.policy, SimulationPolicy::kKiteAndCollect);
    EXPECT_EQ(request.character_id, "deepseek_big_fish");
}

TEST(SimulationRequest, ExplicitOptionsParse) {
    SimulationRequest request;
    std::string err;
    ASSERT_TRUE(parse({"--simulate-run", "--seed", "2", "--policy", "stationary",
                       "--character", "other_fish"},
                      request, err))
        << err;
    EXPECT_TRUE(request.requested);
    EXPECT_EQ(request.seed, 2u);
    EXPECT_EQ(request.policy, SimulationPolicy::kStationary);
    EXPECT_EQ(request.character_id, "other_fish");

    ASSERT_TRUE(parse({"--simulate-run", "--policy", "kite"}, request, err)) << err;
    EXPECT_EQ(request.policy, SimulationPolicy::kKiteAndCollect);
}

TEST(SimulationRequest, SeedBoundariesAreInclusive) {
    std::uint64_t seed = 99;
    EXPECT_TRUE(parse_simulation_seed("0", seed));
    EXPECT_EQ(seed, 0u);
    EXPECT_TRUE(parse_simulation_seed("4294967295", seed));
    EXPECT_EQ(seed, 4294967295ULL);
    EXPECT_TRUE(parse_simulation_seed("0000000007", seed));
    EXPECT_EQ(seed, 7u);
}

TEST(SimulationRequest, MalformedSeedsAreRejected) {
    std::uint64_t seed = 42;
    for (const char *text : {"", "-1", "+1", "1.5", "0x10", " 1", "1 ", "abc",
                             "4294967296", "12345678901", "99999999999999999999"}) {
        EXPECT_FALSE(parse_simulation_seed(text, seed)) << "seed text: " << text;
    }
    EXPECT_EQ(seed, 42u); // a rejected seed never overwrites the value
}

TEST(SimulationRequest, InvalidSeedArgumentIsRejected) {
    SimulationRequest request;
    std::string err;
    EXPECT_FALSE(parse({"--simulate-run", "--seed", "4294967296"}, request, err));
    EXPECT_NE(err.find("--seed"), std::string::npos);
    EXPECT_FALSE(parse({"--simulate-run", "--seed", "-3"}, request, err));
}

TEST(SimulationRequest, UnsupportedPoliciesAreRejected) {
    SimulationRequest request;
    std::string err;
    for (const char *policy : {"", "Kite", "KITE", "random", "stationary ", "kite,stationary"}) {
        EXPECT_FALSE(parse({"--simulate-run", "--policy", policy}, request, err))
            << "policy: " << policy;
        EXPECT_NE(err.find("--policy"), std::string::npos);
    }
}

TEST(SimulationRequest, PolicyNamesAreStable) {
    EXPECT_STREQ(simulation_policy_name(SimulationPolicy::kKiteAndCollect), "kite");
    EXPECT_STREQ(simulation_policy_name(SimulationPolicy::kStationary), "stationary");
}

TEST(SimulationRequest, MissingValuesAreRejected) {
    SimulationRequest request;
    std::string err;
    EXPECT_FALSE(parse({"--simulate-run", "--seed"}, request, err));
    EXPECT_NE(err.find("requires a value"), std::string::npos);
    // A following "--..." token is an option, not a value for --seed.
    EXPECT_FALSE(parse({"--simulate-run", "--seed", "--policy", "kite"}, request, err));
    EXPECT_FALSE(parse({"--simulate-run", "--character"}, request, err));
}

TEST(SimulationRequest, EmptyCharacterIsRejected) {
    SimulationRequest request;
    std::string err;
    EXPECT_FALSE(parse({"--simulate-run", "--character", ""}, request, err));
    EXPECT_NE(err.find("--character"), std::string::npos);
}

TEST(SimulationRequest, DuplicateOptionsAreRejected) {
    SimulationRequest request;
    std::string err;
    EXPECT_FALSE(parse({"--simulate-run", "--seed", "1", "--seed", "2"}, request, err));
    EXPECT_NE(err.find("duplicate"), std::string::npos);
    EXPECT_FALSE(parse({"--simulate-run", "--policy", "kite", "--policy", "kite"},
                       request, err));
}

TEST(SimulationRequest, SimulationOptionsRequireSimulateRun) {
    SimulationRequest request;
    std::string err;
    EXPECT_FALSE(parse({"--seed", "2"}, request, err));
    EXPECT_NE(err.find("--simulate-run"), std::string::npos);
    EXPECT_FALSE(parse({"--policy", "stationary"}, request, err));
    EXPECT_FALSE(parse({"--character", "deepseek_big_fish"}, request, err));
}

TEST(SimulationRequest, UnknownArgWithSimulateRunIsRejected) {
    SimulationRequest request;
    std::string err;
    EXPECT_FALSE(parse({"--simulate-run", "--bogus"}, request, err));
    EXPECT_NE(err.find("--bogus"), std::string::npos);
}

TEST(SimulationRequest, SimulateRunCannotCombineWithGameFlags) {
    SimulationRequest request;
    std::string err;
    EXPECT_FALSE(parse({"--simulate-run", "--integration-test"}, request, err));
    EXPECT_FALSE(parse({"--simulate-run", "--visual-battle"}, request, err));
    EXPECT_FALSE(parse({"--simulate-run", "--visual-weapons"}, request, err));
}

TEST(SimulationRequest, RepeatedParsesAgreeAndRunsAreRepeatable) {
    const std::vector<std::string> args = {"--simulate-run", "--seed", "2",
                                           "--policy", "kite"};
    SimulationRequest first;
    SimulationRequest second;
    std::string err;
    ASSERT_TRUE(parse(args, first, err)) << err;
    ASSERT_TRUE(parse(args, second, err)) << err;
    EXPECT_EQ(first.seed, second.seed);
    EXPECT_EQ(first.policy, second.policy);
    EXPECT_EQ(first.character_id, second.character_id);

    // The parsed request drives the same deterministic run each time.
    const SimulationResult a = run_simulation(fish(), first.seed, first.policy);
    const SimulationResult b = run_simulation(fish(), second.seed, second.policy);
    EXPECT_EQ(a.trace_hash, b.trace_hash);
    EXPECT_EQ(a.frames, b.frames);
    EXPECT_EQ(a.final_state, b.final_state);
}

TEST(SimulationRequest, DirectReuseDoesNotCarryPriorOptionsOrErrors) {
    SimulationRequest request;
    std::string err;
    ASSERT_TRUE(parse_simulation_args({"--simulate-run", "--seed", "9",
                                      "--policy", "stationary"}, request, err));
    ASSERT_TRUE(request.requested);
    ASSERT_FALSE(parse_simulation_args({"--simulate-run", "--seed", "bad"}, request, err));
    ASSERT_FALSE(err.empty());
    ASSERT_TRUE(parse_simulation_args({}, request, err));
    EXPECT_FALSE(request.requested);
    EXPECT_EQ(request.seed, 1u);
    EXPECT_EQ(request.policy, SimulationPolicy::kKiteAndCollect);
    EXPECT_TRUE(err.empty());
}

} // namespace
