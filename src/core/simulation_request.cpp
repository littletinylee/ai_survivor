#include "core/simulation_request.h"

#include <algorithm>

namespace ai_survivors {

// Accepts only plain decimal digits in the unsigned 32-bit range.
bool parse_simulation_seed(const std::string &text, std::uint64_t &seed) {
    if (text.empty() || text.size() > 10) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value > 0xFFFFFFFFULL) {
        return false;
    }
    seed = value;
    return true;
}

const char *simulation_policy_name(SimulationPolicy policy) {
    return policy == SimulationPolicy::kStationary ? "stationary" : "kite";
}

bool parse_simulation_args(const std::vector<std::string> &args,
                           SimulationRequest &request, std::string &err) {
    request = SimulationRequest{};
    err.clear();
    bool simulate = false;
    bool game_flag = false;
    std::string unknown_arg;
    std::vector<std::string> seen;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string &arg = args[i];
        const bool is_option = arg == "--simulate-run" || arg == "--seed" ||
                               arg == "--policy" || arg == "--character";
        if (is_option) {
            if (std::find(seen.begin(), seen.end(), arg) != seen.end()) {
                err = "duplicate " + arg;
                return false;
            }
            seen.push_back(arg);
        }
        if (arg == "--simulate-run") {
            simulate = true;
        } else if (arg == "--seed" || arg == "--policy" || arg == "--character") {
            // A following "--..." token is a missing value, not a value.
            const bool has_value = i + 1 < args.size() &&
                                   args[i + 1].rfind("--", 0) != 0;
            if (!has_value) {
                err = arg + " requires a value";
                return false;
            }
            const std::string value = args[++i];
            if (arg == "--seed") {
                if (!parse_simulation_seed(value, request.seed)) {
                    err = "invalid --seed (expected 0..4294967295): " + value;
                    return false;
                }
            } else if (arg == "--policy") {
                if (value == "kite") {
                    request.policy = SimulationPolicy::kKiteAndCollect;
                } else if (value == "stationary") {
                    request.policy = SimulationPolicy::kStationary;
                } else {
                    err = "invalid --policy (kite|stationary): " + value;
                    return false;
                }
            } else {
                if (value.empty()) {
                    err = "invalid --character: empty id";
                    return false;
                }
                request.character_id = value;
            }
        } else if (arg == "--integration-test" || arg.rfind("--visual-", 0) == 0) {
            game_flag = true;
        } else if (unknown_arg.empty()) {
            unknown_arg = arg;
        }
    }

    const bool value_option_seen =
        std::find(seen.begin(), seen.end(), "--seed") != seen.end() ||
        std::find(seen.begin(), seen.end(), "--policy") != seen.end() ||
        std::find(seen.begin(), seen.end(), "--character") != seen.end();
    if (!simulate) {
        if (value_option_seen) {
            err = "--seed, --policy and --character require --simulate-run";
            return false;
        }
        return true;
    }
    if (!unknown_arg.empty()) {
        err = "unknown argument with --simulate-run: " + unknown_arg;
        return false;
    }
    if (game_flag) {
        err = "--simulate-run cannot be combined with --integration-test or --visual-*";
        return false;
    }
    request.requested = true;
    return true;
}

} // namespace ai_survivors
