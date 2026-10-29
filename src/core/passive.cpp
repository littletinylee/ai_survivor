#include "core/passive.h"
#include "core/character.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ai_survivors {

PassiveRegistry PassiveRegistry::make_default() {
    PassiveRegistry r;
    PassiveSpec none;
    none.id = "none";
    r.add(none);

    PassiveSpec rice;
    rice.id = "free_rice";
    rice.kind = PassiveKind::kXpHeal;
    rice.xp_per_proc = 100.0;
    rice.heal_fraction = 0.03;
    r.add(rice);
    return r;
}

std::string PassiveRegistry::validate(const PassiveSpec &spec) {
    if (!is_valid_identifier(spec.id)) {
        return "invalid passive id";
    }
    switch (spec.kind) {
    case PassiveKind::kNone:
        if (spec.xp_per_proc != 0.0 || spec.heal_fraction != 0.0) {
            return "none passive must have zero parameters";
        }
        return std::string();
    case PassiveKind::kXpHeal:
        if (!std::isfinite(spec.xp_per_proc) || spec.xp_per_proc <= 0.0 ||
            spec.xp_per_proc > kMaxXpPerProc) {
            return "passive xp_per_proc must be finite and in (0, 1e6]";
        }
        if (!std::isfinite(spec.heal_fraction) || spec.heal_fraction <= 0.0 ||
            spec.heal_fraction > 1.0) {
            return "passive heal_fraction must be finite and in (0, 1]";
        }
        return std::string();
    }
    return "unknown passive kind";
}

std::string PassiveRegistry::add(const PassiveSpec &spec) {
    std::string err = validate(spec);
    if (!err.empty()) {
        return err;
    }
    if (find(spec.id) != nullptr) {
        return "duplicate passive id: " + spec.id;
    }
    specs_.push_back(spec);
    return std::string();
}

const PassiveSpec *PassiveRegistry::find(const std::string &id) const {
    for (const PassiveSpec &s : specs_) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

std::uint32_t PassiveRuntime::feed(double collected_xp) {
    if (!active() || !std::isfinite(collected_xp) || collected_xp <= 0.0) {
        return 0;
    }
    // progress_ < threshold <= 1e6 and the feed is <= 1e12: always finite.
    const double total = progress_ + std::min(collected_xp, kMaxFeedXp);
    const double whole = std::floor(total / spec_.xp_per_proc);
    progress_ = std::fmod(total, spec_.xp_per_proc);
    // Count arithmetic may saturate, but a small configured heal fraction
    // must still receive every threshold's contribution. No per-proc loop.
    pending_heal_fraction_ = std::min(
        1.0, pending_heal_fraction_ + std::min(1.0, whole * spec_.heal_fraction));
    const std::uint32_t procs = static_cast<std::uint32_t>(
        std::min(whole, static_cast<double>(kMaxProcsPerFeed)));
    constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
    pending_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        std::uint64_t{pending_} + procs, kMaxProcsPerFeed));
    proc_count_ = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(std::uint64_t{proc_count_} + procs, kMax));
    return procs;
}

double PassiveRuntime::take_heal(double max_hp) {
    const double fraction = pending_heal_fraction_;
    pending_ = 0;
    pending_heal_fraction_ = 0.0;
    if (!active() || fraction <= 0.0 || !std::isfinite(max_hp) || max_hp <= 0.0) {
        return 0.0;
    }
    // More than a full bar cannot restore additional HP. Clamp before the
    // multiplication so even a finite, very large max HP cannot overflow.
    return fraction * max_hp;
}

double PassiveRuntime::progress_fraction() const {
    if (!active()) {
        return 0.0;
    }
    return std::clamp(progress_ / spec_.xp_per_proc, 0.0, 1.0);
}

} // namespace ai_survivors
