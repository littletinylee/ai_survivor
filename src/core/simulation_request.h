#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/run_simulation.h"

namespace ai_survivors {

// Parsed --simulate-run options. Defaults match tools/simulate.ps1.
struct SimulationRequest {
    bool requested = false;
    std::uint64_t seed = 1;
    SimulationPolicy policy = SimulationPolicy::kKiteAndCollect;
    std::string character_id = "deepseek_big_fish";
};

// Accepts only plain decimal digits in the unsigned 32-bit range (0..4294967295).
bool parse_simulation_seed(const std::string &text, std::uint64_t &seed);

// Stable report name for a policy: "kite" or "stationary".
const char *simulation_policy_name(SimulationPolicy policy);

// Parses the user args (everything after "--" on the Godot command line).
// Unknown args are errors only with --simulate-run, so normal game and test
// args are never rejected. Returns false and sets err on malformed input; the
// request is then unspecified. Pure: no Godot types, no global state.
bool parse_simulation_args(const std::vector<std::string> &args,
                           SimulationRequest &request, std::string &err);

} // namespace ai_survivors
