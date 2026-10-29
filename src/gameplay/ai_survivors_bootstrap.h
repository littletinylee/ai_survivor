#pragma once

#include <memory>
#include <string>

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node2d.hpp>

#include <cstdint>
#include <vector>

#include <godot_cpp/classes/button.hpp>

#include "core/character.h"
#include "core/combat.h"
#include "core/game_state.h"
#include "core/game_timer.h"
#include "core/player_controller.h"
#include "core/progression.h"
#include "core/run_step.h"

namespace ai_survivors {

// Phase 1-03 scene layer: main menu -> character selection -> battle arena
// with movement, automatic combat (CombatWorld), XP orbs, levels and native
// upgrade cards (ProgressionSession). Rules live in core/progression.*.
//
// Characters come from the pure C++ CharacterRegistry, loaded from the JSON
// files in res://data/characters. Invalid data is reported loudly and never
// replaced by hardcoded defaults. The node also keeps the opt-in
// --integration-test headless run, and --visual-battle starts a battle
// immediately for screenshots.
class AISurvivorsBootstrap : public godot::Node2D {
    GDCLASS(AISurvivorsBootstrap, godot::Node2D)

public:
    AISurvivorsBootstrap() = default;
    ~AISurvivorsBootstrap() override = default;

    void _ready() override;
    void _physics_process(double delta) override;
    void _draw() override;

protected:
    static void _bind_methods();

private:
    enum class Screen { kMenu, kCharacterSelect, kBattle };

    void build_ui();
    void load_characters();
    void populate_character_buttons();
    void show_screen(Screen screen);
    void start_battle(const std::string &character_id);
    void refresh_battle_ui();
    // One Playing frame via core/run_step advance_run (movement, combat,
    // timer, XP, upgrade offer). Does nothing in any other state.
    void advance_frame(double delta, double input_x = 0.0, double input_y = 0.0);
    // Advances movement-less frames in fixed steps via advance_frame.
    void simulate_battle(double seconds);

    // Presentation only: rebuilds card buttons when an offer opens or changes.
    void present_upgrade(UpgradePrep prep);
    void show_offer();
    void clear_card_buttons();
    void on_card_pressed(const godot::String &card_id, int64_t generation);

    void on_start_pressed();
    void on_back_to_menu_pressed();
    void on_resume_pressed();
    void on_restart_pressed();
    void on_character_pressed(int index);

    bool run_core_checks();
    bool run_native_checks();
    bool run_progression_checks();

    // Run-local state; recreated for every battle.
    WeaponRegistry weapon_registry_ = WeaponRegistry::make_default();
    std::unique_ptr<ProgressionSession> progression_;
    std::vector<godot::Button *> card_buttons_;
    GameStateManager states_;
    std::unique_ptr<GameTimer> timer_;
    PlayerController player_{kArenaHalfWidth, kArenaHalfHeight, kPlayerRadius,
                             1.0};
    const CharacterDefinition *active_character_ = nullptr;
    double facing_ = 1.0;
    std::unique_ptr<CombatWorld> world_;
    std::uint64_t run_counter_ = 0;
    bool escape_was_down_ = false;
    Screen screen_ = Screen::kMenu;

    CharacterRegistry registry_;
    std::string load_error_;

    static constexpr double kArenaHalfWidth = 520.0;
    static constexpr double kArenaHalfHeight = 290.0;
    static constexpr double kPlayerRadius = 54.0;

    godot::Label *status_label_ = nullptr;
    godot::Label *error_label_ = nullptr;
    godot::Label *battle_label_ = nullptr;
    godot::Control *menu_panel_ = nullptr;
    godot::Control *select_panel_ = nullptr;
    godot::Control *battle_panel_ = nullptr;
    godot::Control *pause_panel_ = nullptr;
    godot::Control *character_list_ = nullptr;
    godot::Control *start_button_ = nullptr;
    godot::Control *upgrade_panel_ = nullptr;
    godot::Label *upgrade_title_ = nullptr;
    godot::Label *xp_label_ = nullptr;
    godot::Label *overlay_title_ = nullptr;
    godot::Control *resume_button_ = nullptr;
};

} // namespace ai_survivors
