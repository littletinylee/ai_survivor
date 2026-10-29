#pragma once

#include <string>
#include <vector>

namespace ai_survivors {

// Static, data-driven description of a playable character. Asset fields are
// string references only; empty means "explicit placeholder, draw geometry".
struct CharacterDefinition {
    std::string id;
    std::string name;
    std::string description;
    std::string portrait_asset;
    std::string sprite_asset;
    std::string animation_asset;
    double max_hp = 100.0;
    double move_speed = 220.0;
    double armor = 0.0;
    double damage_multiplier = 1.0;
    double attack_speed_multiplier = 1.0;
    double crit_chance = 0.0;
    double xp_multiplier = 1.0;
    double pickup_radius = 80.0;
    std::string initial_weapon_id;
    std::string passive_id;
    std::string passive_description;
};

// Returns an empty string when valid, otherwise a human readable reason.
std::string validate_character(const CharacterDefinition &character);

// Id grammar: 1..64 chars of [a-z0-9_], first char a letter.
bool is_valid_identifier(const std::string &id);

class CharacterRegistry {
public:
    // Returns empty string on success; on failure nothing is added.
    std::string add(const CharacterDefinition &character);

    const CharacterDefinition *find(const std::string &id) const;
    const std::vector<CharacterDefinition> &characters() const {
        return characters_;
    }
    std::size_t size() const { return characters_.size(); }
    bool empty() const { return characters_.empty(); }

private:
    std::vector<CharacterDefinition> characters_;
};

} // namespace ai_survivors
