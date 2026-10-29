#include "core/character.h"

#include <cmath>

namespace ai_survivors {

bool is_valid_identifier(const std::string &id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    if (id[0] < 'a' || id[0] > 'z') {
        return false;
    }
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string validate_character(const CharacterDefinition &c) {
    if (!is_valid_identifier(c.id)) {
        return "invalid character id";
    }
    if (c.name.empty()) {
        return "empty name";
    }
    if (c.description.empty()) {
        return "empty description";
    }
    if (!is_valid_identifier(c.initial_weapon_id)) {
        return "invalid initial weapon id";
    }
    if (!is_valid_identifier(c.passive_id)) {
        return "invalid passive id";
    }
    if (c.passive_description.empty()) {
        return "empty passive description";
    }
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    const auto non_negative = [](double v) {
        return std::isfinite(v) && v >= 0.0;
    };
    if (!positive(c.max_hp)) return "max_hp must be finite and > 0";
    if (!positive(c.move_speed)) return "move_speed must be finite and > 0";
    if (!non_negative(c.armor)) return "armor must be finite and >= 0";
    if (!positive(c.damage_multiplier)) return "damage_multiplier must be finite and > 0";
    if (!positive(c.attack_speed_multiplier)) return "attack_speed_multiplier must be finite and > 0";
    if (!std::isfinite(c.crit_chance) || c.crit_chance < 0.0 || c.crit_chance > 1.0) {
        return "crit_chance must be within [0, 1]";
    }
    if (!positive(c.xp_multiplier)) return "xp_multiplier must be finite and > 0";
    if (!non_negative(c.pickup_radius)) return "pickup_radius must be finite and >= 0";
    return std::string();
}

std::string CharacterRegistry::add(const CharacterDefinition &character) {
    std::string error = validate_character(character);
    if (!error.empty()) {
        return error;
    }
    if (find(character.id) != nullptr) {
        return "duplicate character id: " + character.id;
    }
    characters_.push_back(character);
    return std::string();
}

const CharacterDefinition *CharacterRegistry::find(const std::string &id) const {
    for (const CharacterDefinition &c : characters_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

} // namespace ai_survivors
