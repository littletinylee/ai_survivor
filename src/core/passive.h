#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ai_survivors {

enum class PassiveKind {
    kNone,
    kXpHeal, // every xp_per_proc collected XP restores heal_fraction of max HP
};

// Immutable, data-driven passive description. Characters reference one by id.
struct PassiveSpec {
    std::string id;
    PassiveKind kind = PassiveKind::kNone;
    double xp_per_proc = 0.0;   // kXpHeal: actual collected XP per proc
    double heal_fraction = 0.0; // kXpHeal: fraction of current max HP per proc
};

class PassiveRegistry {
public:
    static constexpr double kMaxXpPerProc = 1e6;

    // Contains "none" and "free_rice".
    static PassiveRegistry make_default();

    // Empty string on success. Rejects empty/duplicate ids and parameters that
    // are non-finite or out of range for the kind.
    static std::string validate(const PassiveSpec &spec);
    std::string add(const PassiveSpec &spec);
    const PassiveSpec *find(const std::string &id) const;
    const std::vector<PassiveSpec> &passives() const { return specs_; }

private:
    std::vector<PassiveSpec> specs_;
};

// Per-run state of one passive. A plain value owned by the run's
// ProgressionSession: a new session starts from zero, nothing is global.
class PassiveRuntime {
public:
    // Largest XP one feed may contribute; keeps arithmetic finite.
    static constexpr double kMaxFeedXp = 1e12;
    // Display counters saturate; healing remains based on every threshold.
    static constexpr std::uint32_t kMaxProcsPerFeed = UINT32_MAX;

    PassiveRuntime() = default;
    // The spec must have passed PassiveRegistry::validate.
    explicit PassiveRuntime(const PassiveSpec &spec) : spec_(spec) {}

    const PassiveSpec &spec() const { return spec_; }
    bool active() const { return spec_.kind == PassiveKind::kXpHeal; }

    // Adds actual collected XP (already multiplied). O(1): thresholds are
    // computed arithmetically, never by looping. Non-finite / non-positive
    // input and inactive passives are ignored. Returns procs newly queued.
    std::uint32_t feed(double collected_xp);

    // Procs queued by feed() and not yet consumed.
    std::uint32_t pending_procs() const { return pending_; }
    // Healing requested, capped to one full HP bar, then
    // clears the queue. Procs are consumed even when the heal ends up being
    // zero (full HP), so nothing is banked. Non-finite max HP yields 0.
    double take_heal(double max_hp);

    // Read-only HUD numbers. progress() is in [0, threshold).
    double progress() const { return progress_; }
    double threshold() const { return active() ? spec_.xp_per_proc : 0.0; }
    double progress_fraction() const;
    std::uint32_t proc_count() const { return proc_count_; } // saturating

private:
    PassiveSpec spec_;
    double progress_ = 0.0;
    double pending_heal_fraction_ = 0.0;
    std::uint32_t pending_ = 0;
    std::uint32_t proc_count_ = 0;
};

} // namespace ai_survivors
