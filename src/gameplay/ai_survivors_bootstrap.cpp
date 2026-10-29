#include "gameplay/ai_survivors_bootstrap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "core/game_state.h"
#include "core/game_timer.h"
#include "core/run_simulation.h"

namespace ai_survivors {

using godot::Button;
using godot::Color;
using godot::Control;
using godot::Dictionary;
using godot::Label;
using godot::OS;
using godot::PackedStringArray;
using godot::String;
using godot::StringName;
using godot::Variant;
using godot::Vector2;

namespace {

const char *const kCharacterDir = "res://data/characters";
constexpr float kArenaCenterX = 550.0f;
constexpr float kArenaCenterY = 360.0f;
// Display heights only; hitbox radius and gameplay rules are unchanged.
constexpr float kPlayerSpriteHeight = 108.0f;
constexpr float kPortraitHeight = 88.0f;

String to_godot(const std::string &s) { return String::utf8(s.c_str()); }
std::string to_std(const String &s) { return std::string(s.utf8().get_data()); }

bool read_string(const Dictionary &d, const char *key, std::string &out,
                 std::string &err) {
    if (!d.has(key)) {
        err = std::string("missing field: ") + key;
        return false;
    }
    const Variant v = d.get(key, Variant());
    if (v.get_type() != Variant::STRING) {
        err = std::string("field must be a string: ") + key;
        return false;
    }
    out = to_std(String(v));
    return true;
}

bool read_number(const Dictionary &d, const char *key, double &out,
                 std::string &err) {
    if (!d.has(key)) {
        err = std::string("missing field: ") + key;
        return false;
    }
    const Variant v = d.get(key, Variant());
    if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT) {
        err = std::string("field must be a number: ") + key;
        return false;
    }
    out = static_cast<double>(v);
    return true;
}

// Returns empty string on success.
std::string parse_character(const Dictionary &d, CharacterDefinition &c) {
    std::string err;
    const bool ok =
        read_string(d, "id", c.id, err) && read_string(d, "name", c.name, err) &&
        read_string(d, "description", c.description, err) &&
        read_string(d, "portrait_asset", c.portrait_asset, err) &&
        read_string(d, "sprite_asset", c.sprite_asset, err) &&
        read_string(d, "animation_asset", c.animation_asset, err) &&
        read_number(d, "max_hp", c.max_hp, err) &&
        read_number(d, "move_speed", c.move_speed, err) &&
        read_number(d, "armor", c.armor, err) &&
        read_number(d, "damage_multiplier", c.damage_multiplier, err) &&
        read_number(d, "attack_speed_multiplier", c.attack_speed_multiplier,
                    err) &&
        read_number(d, "crit_chance", c.crit_chance, err) &&
        read_number(d, "xp_multiplier", c.xp_multiplier, err) &&
        read_number(d, "pickup_radius", c.pickup_radius, err) &&
        read_string(d, "initial_weapon_id", c.initial_weapon_id, err) &&
        read_string(d, "passive_id", c.passive_id, err) &&
        read_string(d, "passive_description", c.passive_description, err);
    return ok ? std::string() : err;
}

Label *make_label(Control *parent, const char *text, int size, float x,
                  float y, float w, float h, bool center) {
    Label *label = memnew(Label);
    label->set_text(String::utf8(text));
    label->set_position(Vector2(x, y));
    label->set_size(Vector2(w, h));
    label->add_theme_font_size_override("font_size", size);
    label->add_theme_color_override("font_color",
                                    Color(0.90f, 0.95f, 1.0f, 1.0f));
    if (center) {
        label->set_horizontal_alignment(godot::HORIZONTAL_ALIGNMENT_CENTER);
    }
    parent->add_child(label);
    return label;
}

Button *make_button(Control *parent, const String &text, int size, float x,
                    float y, float w, float h) {
    Button *button = memnew(Button);
    button->set_text(text);
    button->set_position(Vector2(x, y));
    button->set_size(Vector2(w, h));
    button->add_theme_font_size_override("font_size", size);
    parent->add_child(button);
    return button;
}

Control *make_panel(Control *parent, const char *name) {
    Control *panel = memnew(Control);
    panel->set_name(name);
    panel->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    parent->add_child(panel);
    panel->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    return panel;
}

Button *make_button(Control *parent, const char *text, int size, float x,
                    float y, float w, float h) {
    return make_button(parent, String::utf8(text), size, x, y, w, h);
}

bool key_down(godot::Key key) {
    return godot::Input::get_singleton()->is_physical_key_pressed(key);
}

bool has_pixels(const godot::Ref<godot::Texture2D> &texture) {
    return texture.is_valid() && texture->get_width() > 0 &&
           texture->get_height() > 0;
}

// --simulate-run options; defaults match tools/simulate.ps1.
struct SimulationRequest {
    bool requested = false;
    std::uint64_t seed = 1;
    SimulationPolicy policy = SimulationPolicy::kKiteAndCollect;
    std::string character_id = "deepseek_big_fish";
};

// Accepts only plain decimal digits in the unsigned 32-bit range.
bool parse_seed(const std::string &text, std::uint64_t &seed) {
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

// Unknown user args are errors only in simulation mode, so normal game and
// test args are never rejected here.
bool parse_simulation_args(const PackedStringArray &args,
                           SimulationRequest &request, std::string &err) {
    bool simulate = false;
    bool game_flag = false;
    std::string unknown_arg;
    std::vector<std::string> seen;
    for (int64_t i = 0; i < args.size(); ++i) {
        const std::string arg = to_std(args[i]);
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
                                   to_std(args[i + 1]).rfind("--", 0) != 0;
            if (!has_value) {
                err = arg + " requires a value";
                return false;
            }
            const std::string value = to_std(args[++i]);
            if (arg == "--seed") {
                if (!parse_seed(value, request.seed)) {
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

const char *policy_name(SimulationPolicy policy) {
    return policy == SimulationPolicy::kStationary ? "stationary" : "kite";
}

// A Lost run is a legitimate result. Only structural and invariant failures
// make the run invalid; the outcome itself is never forced.
bool simulation_is_valid(const SimulationResult &r) {
    const bool terminal = r.final_state == GameState::kWon ||
                          r.final_state == GameState::kLost;
    // Won is only reachable at the 1200 s logic boundary.
    const bool won_at_boundary =
        r.final_state == GameState::kWon
            ? r.survival_seconds == GameTimer::kWinDurationSeconds && r.final_hp > 0.0
            : r.survival_seconds < GameTimer::kWinDurationSeconds && r.final_hp == 0.0;
    return r.error.empty() && r.terminated && terminal && won_at_boundary &&
           !r.hit_iteration_limit && !r.stalled && r.invalid_selections == 0 &&
           r.frozen_violations == 0 &&
           r.terminal_violations == 0 && r.peak_enemies <= 256 &&
           r.peak_projectiles <= 512 && r.peak_orbs <= 512 &&
           r.peak_weapons <= 6 && r.peak_zones <= 32;
}

// Runs the pure core on the loaded character and prints one report line.
// Returns the process exit code: 0 only for a valid terminal run.
int run_simulation_report(const CharacterRegistry &registry,
                          const SimulationRequest &request) {
    const CharacterDefinition *character = registry.find(request.character_id);
    if (character == nullptr) {
        godot::UtilityFunctions::push_error(
            to_godot("simulation character not loaded: " + request.character_id));
        return 1;
    }
    const SimulationResult r =
        run_simulation(*character, request.seed, request.policy);

    // Hex string: a uint64 would lose precision as a JSON number.
    char hash[24];
    std::snprintf(hash, sizeof(hash), "%016llx",
                  static_cast<unsigned long long>(r.trace_hash));

    Dictionary report;
    report["requested_seed"] = static_cast<int64_t>(request.seed);
    report["character_id"] = to_godot(character->id);
    report["policy"] = to_godot(policy_name(request.policy));
    report["normal_character"] = true;
    report["final_state"] = to_godot(to_string(r.final_state));
    report["survival_seconds"] = r.survival_seconds;
    report["hp"] = r.final_hp;
    report["maxhp"] = r.max_hp;
    report["kills"] = static_cast<int64_t>(r.kills);
    report["level"] = r.final_level;
    report["xp"] = r.xp_gained;
    report["cards"] = r.cards_picked;
    report["invalid_selections"] = r.invalid_selections;
    report["peak_pending_levels"] = r.peak_pending_levels;
    report["peak_enemies"] = static_cast<int64_t>(r.peak_enemies);
    report["peak_projectiles"] = static_cast<int64_t>(r.peak_projectiles);
    report["peak_orbs"] = static_cast<int64_t>(r.peak_orbs);
    report["peak_weapons"] = static_cast<int64_t>(r.peak_weapons);
    report["peak_zones"] = static_cast<int64_t>(r.peak_zones);
    report["frames"] = static_cast<int64_t>(r.frames);
    report["trace_hash"] = String::utf8(hash);
    report["error"] = to_godot(r.error);
    report["iteration_limit"] = r.hit_iteration_limit;
    report["stalled"] = r.stalled;
    report["frozen_violations"] = r.frozen_violations;
    report["terminal_violations"] = r.terminal_violations;

    godot::UtilityFunctions::print(
        String("AI_SURVIVORS_SIMULATION_REPORT ") + godot::JSON::stringify(report));

    if (!simulation_is_valid(r)) {
        godot::UtilityFunctions::push_error(
            to_godot("simulation report is not a valid terminal run"));
        return 1;
    }
    return 0;
}

} // namespace

void AISurvivorsBootstrap::_bind_methods() {}

void AISurvivorsBootstrap::load_characters() {
    registry_ = CharacterRegistry();
    art_.clear();
    texture_cache_.clear();
    load_error_.clear();

    godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(kCharacterDir);
    if (dir.is_null()) {
        load_error_ = std::string("cannot open ") + kCharacterDir;
        godot::UtilityFunctions::push_error(to_godot(load_error_));
        return;
    }
    PackedStringArray files = dir->get_files();
    files.sort();
    for (int64_t i = 0; i < files.size(); ++i) {
        const String file = files[i];
        if (!file.ends_with(".json")) {
            continue;
        }
        const String path = String(kCharacterDir) + "/" + file;
        const std::string name = to_std(file);
        const String text = godot::FileAccess::get_file_as_string(path);
        if (text.is_empty()) {
            load_error_ = name + ": unreadable or empty file";
            break;
        }
        const Variant parsed = godot::JSON::parse_string(text);
        if (parsed.get_type() != Variant::DICTIONARY) {
            load_error_ = name + ": invalid JSON object";
            break;
        }
        CharacterDefinition character;
        std::string err = parse_character(Dictionary(parsed), character);
        if (err.empty()) {
            err = registry_.add(character);
        }
        if (!err.empty()) {
            load_error_ = name + ": " + err;
            break;
        }
    }
    if (load_error_.empty() && registry_.empty()) {
        load_error_ = std::string("no character definitions in ") + kCharacterDir;
    }
    if (load_error_.empty()) {
        load_character_art();
    }
    if (!load_error_.empty()) {
        godot::UtilityFunctions::push_error(
            to_godot("Character load failed: " + load_error_));
    }
}

void AISurvivorsBootstrap::load_character_art() {
    for (const CharacterDefinition &c : registry_.characters()) {
        // Empty asset means "not configured": no portrait, or the procedural
        // fallback for the sprite. A configured path that fails is an error.
        CharacterArt art;
        std::string err;
        if (!c.portrait_asset.empty()) {
            art.portrait = load_texture(c.portrait_asset, err);
            if (!err.empty()) {
                load_error_ = c.id + " portrait_asset: " + err;
                return;
            }
        }
        if (!c.sprite_asset.empty()) {
            art.sprite = load_texture(c.sprite_asset, err);
            if (!err.empty()) {
                load_error_ = c.id + " sprite_asset: " + err;
                return;
            }
        }
        art_[c.id] = art;
    }
}

godot::Ref<godot::Texture2D> AISurvivorsBootstrap::load_texture(const std::string &path,
                                                              std::string &err) {
    const auto cached = texture_cache_.find(path);
    if (cached != texture_cache_.end()) {
        return cached->second;
    }
    godot::Ref<godot::Texture2D> texture =
        godot::ResourceLoader::get_singleton()->load(to_godot(path));
    if (!has_pixels(texture)) {
        err = "cannot load a non-empty Texture2D from " + path;
        return godot::Ref<godot::Texture2D>();
    }
    texture_cache_[path] = texture;
    return texture;
}

void AISurvivorsBootstrap::build_ui() {
    Control *ui_root = memnew(Control);
    ui_root->set_name("AISurvivorsUiRoot");
    ui_root->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    add_child(ui_root);
    ui_root->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

    // Main menu.
    menu_panel_ = make_panel(ui_root, "MenuPanel");
    status_label_ = make_label(menu_panel_, "AI 幸存者", 56, 0, 130, 1100,
                               80, true);
    make_label(menu_panel_, "选择你的伙伴，开始冒险",
               20, 0, 215, 1100, 30, true);
    start_button_ = make_button(menu_panel_, "开始游戏", 30, 400, 300, 300, 70);
    start_button_->connect(
        "pressed", callable_mp(this, &AISurvivorsBootstrap::on_start_pressed));
    error_label_ = make_label(menu_panel_, "", 20, 100, 400, 900, 120, true);
    error_label_->add_theme_color_override("font_color",
                                           Color(1.0f, 0.4f, 0.4f, 1.0f));

    // Character selection (buttons generated from the registry).
    select_panel_ = make_panel(ui_root, "SelectPanel");
    make_label(select_panel_, "选择角色", 40, 0, 60, 1100, 60, true);
    auto *scroll = memnew(godot::ScrollContainer);
    select_panel_->add_child(scroll);
    scroll->set_position(Vector2(250, 145));
    scroll->set_size(Vector2(620, 420));
    auto *list = memnew(godot::VBoxContainer);
    scroll->add_child(list);
    list->set_custom_minimum_size(Vector2(600, 0));
    character_list_ = list;
    Button *back = make_button(select_panel_, "返回", 24, 40, 590, 160, 50);
    back->connect("pressed", callable_mp(this, &AISurvivorsBootstrap::
                                                   on_back_to_menu_pressed));

    // Battle HUD.
    battle_panel_ = make_panel(ui_root, "BattlePanel");
    battle_label_ = make_label(battle_panel_, "", 22, 20, 12, 1000, 40, false);
    make_label(battle_panel_, "WASD / 方向键移动    Esc 暂停", 16, 20, 650, 600,
               24, false);

    xp_label_ = make_label(battle_panel_, "", 16, 330, 48, 400, 24, false);
    weapons_label_ = make_label(battle_panel_, "", 16, 20, 618, 1060, 28, false);

    upgrade_panel_ = make_panel(battle_panel_, "UpgradePanel");
    upgrade_title_ = make_label(upgrade_panel_, "升级！选择一项强化", 40, 0, 110,
                                1100, 60, true);
    upgrade_panel_->set_visible(false);

    pause_panel_ = make_panel(battle_panel_, "PausePanel");
    overlay_title_ = make_label(pause_panel_, "已暂停", 48, 0, 190, 1100, 70, true);
    Button *resume = make_button(pause_panel_, "继续", 26, 400, 280, 300, 56);
    resume_button_ = resume;
    resume->connect("pressed",
                    callable_mp(this, &AISurvivorsBootstrap::on_resume_pressed));
    Button *restart = make_button(pause_panel_, "重新开始", 26, 400, 350, 300, 56);
    restart->connect("pressed", callable_mp(this, &AISurvivorsBootstrap::
                                                      on_restart_pressed));
    Button *menu = make_button(pause_panel_, "返回主菜单", 26, 400, 420, 300, 56);
    menu->connect("pressed", callable_mp(this, &AISurvivorsBootstrap::
                                                   on_back_to_menu_pressed));
}

void AISurvivorsBootstrap::populate_character_buttons() {
    const std::vector<CharacterDefinition> &list = registry_.characters();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const CharacterDefinition &c = list[i];
        char stats[160];
        std::snprintf(stats, sizeof(stats), "HP %.0f  速度 %.0f  护甲 %.0f",
                      c.max_hp, c.move_speed, c.armor);
        const String text = to_godot(c.name) + "\n" + to_godot(c.description) +
                            "\n" + String::utf8(stats);
        Button *button = make_button(character_list_, text, 20, 250.0f,
                                     150.0f + 110.0f * static_cast<float>(i),
                                     600, 100);
        button->set_custom_minimum_size(Vector2(600, 140));
        button->set_autowrap_mode(godot::TextServer::AUTOWRAP_WORD_SMART);
        const auto art = art_.find(c.id);
        if (art != art_.end() && has_pixels(art->second.portrait)) {
            // icon_max_width caps the width; the icon's height follows its aspect.
            const double aspect = static_cast<double>(art->second.portrait->get_width()) /
                                  static_cast<double>(art->second.portrait->get_height());
            button->set_button_icon(art->second.portrait);
            button->add_theme_constant_override(
                "icon_max_width",
                static_cast<int>(std::lround(kPortraitHeight * aspect)));
        }
        button->connect("pressed",
                        callable_mp(this,
                                    &AISurvivorsBootstrap::on_character_pressed)
                            .bind(static_cast<int>(i)));
    }
}

void AISurvivorsBootstrap::show_screen(Screen screen) {
    screen_ = screen;
    menu_panel_->set_visible(screen == Screen::kMenu);
    select_panel_->set_visible(screen == Screen::kCharacterSelect);
    battle_panel_->set_visible(screen == Screen::kBattle);
    const GameState st = states_.state();
    const bool overlay = screen == Screen::kBattle &&
                         (st == GameState::kPaused || st == GameState::kWon ||
                          st == GameState::kLost);
    pause_panel_->set_visible(overlay);
    upgrade_panel_->set_visible(screen == Screen::kBattle &&
                                st == GameState::kUpgrade);
    if (overlay) {
        overlay_title_->set_text(String::utf8(st == GameState::kLost  ? "失败"
                                              : st == GameState::kWon ? "胜利"
                                                                      : "已暂停"));
        resume_button_->set_visible(st == GameState::kPaused);
    }
    queue_redraw();
}

void AISurvivorsBootstrap::start_battle(const std::string &character_id) {
    const CharacterDefinition *character = registry_.find(character_id);
    if (character == nullptr) {
        godot::UtilityFunctions::push_error("unknown character id");
        return;
    }
    auto session = std::make_unique<ProgressionSession>(
        *character, weapon_registry_, 0x51D00000ULL + run_counter_);
    if (!session->init_error().empty()) {
        godot::UtilityFunctions::push_error(to_godot(session->init_error()));
        return;
    }
    // Combat starts with the weapon(s) actually selected, not a fixed bubble.
    auto world = std::make_unique<CombatWorld>(
        CombatStats::from_character(*character), 0xA15D0000ULL + run_counter_);
    const std::string loadout_error = initialize_run_loadout(*world, *session);
    if (!loadout_error.empty()) {
        godot::UtilityFunctions::push_error(to_godot(loadout_error));
        return;
    }
    ++run_counter_;
    clear_card_buttons();
    progression_ = std::move(session);
    active_character_ = character;
    const auto art = art_.find(character->id);
    active_sprite_ = art != art_.end() ? art->second.sprite
                                       : godot::Ref<godot::Texture2D>();
    // Run-local state is recreated for each battle.
    states_ = GameStateManager();
    timer_ = std::make_unique<GameTimer>(states_);
    states_.start_run();
    player_.reset();
    player_.set_move_speed(progression_->stats().move_speed);
    // Fixed base seed plus a per-session run counter: reproducible, yet
    // restarts are not identical copies.
    world_ = std::move(world);
    facing_ = 1.0;
    escape_was_down_ = key_down(godot::KEY_ESCAPE);
    show_screen(Screen::kBattle);
    refresh_battle_ui();
}

void AISurvivorsBootstrap::refresh_battle_ui() {
    if (active_character_ == nullptr || !timer_ || !world_ ||
        battle_label_ == nullptr) {
        return;
    }
    const int elapsed = static_cast<int>(timer_->elapsed_seconds());
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "  HP %.0f/%.0f   击杀 %u   时间 %02d:%02d / 20:00",
                  std::ceil(world_->hp()), world_->max_hp(), world_->kill_count(),
                  elapsed / 60, elapsed % 60);
    battle_label_->set_text(to_godot(active_character_->name) + String::utf8(buf));
    if (progression_ && xp_label_ != nullptr) {
        char xp[96];
        std::snprintf(xp, sizeof(xp), "等级 %d   经验 %.0f / %.0f",
                      progression_->levels().level(), progression_->levels().xp(),
                      progression_->levels().next_threshold());
        xp_label_->set_text(String::utf8(xp));
        std::string text;
        for (const WeaponInventory::Entry &w : progression_->inventory().entries()) {
            const WeaponDefinition *def = weapon_registry_.find(w.id);
            text += "   " + (def != nullptr ? def->name : w.id) + " Lv" +
                    std::to_string(w.level);
        }
        weapons_label_->set_text(String::utf8(text.c_str()));
    }
}

void AISurvivorsBootstrap::advance_frame(double delta, double input_x,
                                         double input_y) {
    if (states_.state() != GameState::kPlaying || !world_ || !timer_ ||
        !progression_) {
        return;
    }
    const RunStepResult step = advance_run(states_, *timer_, player_, *world_,
                                           *progression_, delta, input_x,
                                           input_y);
    if (step.status == RunStepStatus::kRejected) {
        return;
    }
    if (input_x != 0.0) {
        facing_ = input_x > 0.0 ? 1.0 : -1.0;
    }
    // Buttons are only touched when an upgrade actually opened this frame.
    if (step.upgrade != UpgradePrep::kNoUpgrade) {
        present_upgrade(step.upgrade);
    }
}

void AISurvivorsBootstrap::simulate_battle(double seconds) {
    const double step = 1.0 / 60.0;
    for (double t = 0.0; t < seconds && states_.state() == GameState::kPlaying;
         t += step) {
        advance_frame(step);
    }
    show_screen(Screen::kBattle);
    refresh_battle_ui();
}

void AISurvivorsBootstrap::present_upgrade(UpgradePrep prep) {
    clear_card_buttons();
    if (prep == UpgradePrep::kOfferActive) {
        show_offer();
    }
    show_screen(Screen::kBattle);
}

void AISurvivorsBootstrap::clear_card_buttons() {
    for (Button *b : card_buttons_) {
        b->queue_free();
    }
    card_buttons_.clear();
}

void AISurvivorsBootstrap::show_offer() {
    const UpgradeOffer &offer = progression_->cards().current_offer();
    char title[96];
    std::snprintf(title, sizeof(title), "升级！选择一项强化（剩余 %d 次）",
                  progression_->levels().pending_levels());
    upgrade_title_->set_text(String::utf8(title));
    for (std::size_t i = 0; i < offer.card_ids.size(); ++i) {
        const CardDefinition *card = progression_->cards().find(offer.card_ids[i]);
        if (card == nullptr) {
            continue;
        }
        Button *button = make_button(
            upgrade_panel_, to_godot(card->name) + "\n\n" + to_godot(card->description),
            22, 120.0f + 290.0f * static_cast<float>(i), 200.0f, 270.0f, 240.0f);
        button->set_autowrap_mode(godot::TextServer::AUTOWRAP_ARBITRARY);
        button->connect("pressed",
                        callable_mp(this, &AISurvivorsBootstrap::on_card_pressed)
                            .bind(to_godot(card->id),
                                  static_cast<int64_t>(offer.generation)));
        card_buttons_.push_back(button);
    }
}

void AISurvivorsBootstrap::on_card_pressed(const String &card_id,
                                           int64_t generation) {
    if (!progression_ || !world_) {
        return;
    }
    // Stale / repeated / invalid callbacks are rejected without mutation.
    const ChooseRunResult chosen = choose_run_upgrade(
        states_, *world_, player_, *progression_,
        static_cast<std::uint64_t>(generation), to_std(card_id));
    if (chosen.result != ChooseResult::kApplied) {
        return;
    }
    present_upgrade(chosen.next);
    refresh_battle_ui();
}

void AISurvivorsBootstrap::on_start_pressed() {
    if (!load_error_.empty() || registry_.empty()) {
        return;
    }
    show_screen(Screen::kCharacterSelect);
}

void AISurvivorsBootstrap::on_back_to_menu_pressed() {
    clear_card_buttons();
    progression_.reset();
    states_ = GameStateManager();
    timer_.reset();
    world_.reset();
    active_character_ = nullptr;
    active_sprite_ = godot::Ref<godot::Texture2D>();
    show_screen(Screen::kMenu);
}

void AISurvivorsBootstrap::on_resume_pressed() {
    if (states_.resume()) {
        show_screen(Screen::kBattle);
    }
}

void AISurvivorsBootstrap::on_restart_pressed() {
    if (active_character_ != nullptr) {
        start_battle(active_character_->id);
    }
}

void AISurvivorsBootstrap::on_character_pressed(int index) {
    const std::vector<CharacterDefinition> &list = registry_.characters();
    if (index >= 0 && static_cast<std::size_t>(index) < list.size()) {
        start_battle(list[static_cast<std::size_t>(index)].id);
    }
}

void AISurvivorsBootstrap::_physics_process(double delta) {
    if (screen_ != Screen::kBattle || !timer_ || !world_ ||
        active_character_ == nullptr) {
        return;
    }
    const bool escape_down = key_down(godot::KEY_ESCAPE);
    if (escape_down && !escape_was_down_) {
        if (states_.state() == GameState::kPlaying) {
            states_.pause();
        } else if (states_.state() == GameState::kPaused) {
            states_.resume();
        }
        show_screen(Screen::kBattle);
    }
    escape_was_down_ = escape_down;

    double ix = 0.0;
    double iy = 0.0;
    if (key_down(godot::KEY_A) || key_down(godot::KEY_LEFT)) ix -= 1.0;
    if (key_down(godot::KEY_D) || key_down(godot::KEY_RIGHT)) ix += 1.0;
    if (key_down(godot::KEY_W) || key_down(godot::KEY_UP)) iy -= 1.0;
    if (key_down(godot::KEY_S) || key_down(godot::KEY_DOWN)) iy += 1.0;

    if (states_.state() == GameState::kPlaying) {
        advance_frame(delta, ix, iy);
        if (states_.state() != GameState::kPlaying) {
            show_screen(Screen::kBattle); // defeat / victory overlay
        }
    }
    refresh_battle_ui();
    queue_redraw();
}

void AISurvivorsBootstrap::_draw() {
    if (screen_ != Screen::kBattle) {
        return;
    }
    const Vector2 center(kArenaCenterX, kArenaCenterY);
    const Vector2 half(static_cast<float>(kArenaHalfWidth),
                       static_cast<float>(kArenaHalfHeight));
    draw_rect(godot::Rect2(center - half, half * 2.0f),
              Color(0.05f, 0.12f, 0.20f, 1.0f), true);
    draw_rect(godot::Rect2(center - half, half * 2.0f),
              Color(0.35f, 0.65f, 0.85f, 1.0f), false, 3.0f);

    if (progression_) {
        // Bright green gems: distinct from cyan shots and red enemies.
        for (const XpOrb &o : progression_->experience().orbs()) {
            const Vector2 op =
                center + Vector2(static_cast<float>(o.position.x),
                                 static_cast<float>(o.position.y));
            godot::PackedVector2Array gem;
            gem.push_back(op + Vector2(0.0f, -9.0f));
            gem.push_back(op + Vector2(7.0f, 0.0f));
            gem.push_back(op + Vector2(0.0f, 9.0f));
            gem.push_back(op + Vector2(-7.0f, 0.0f));
            draw_colored_polygon(gem, Color(0.25f, 1.0f, 0.25f, 1.0f));
            draw_polyline(gem, Color(0.85f, 1.0f, 0.75f, 1.0f), 1.5f);
            draw_line(op + Vector2(-7.0f, 0.0f), op + Vector2(0.0f, -9.0f),
                      Color(0.85f, 1.0f, 0.75f, 1.0f), 1.5f);
        }
        // XP bar under the HUD line.
        const double need = progression_->levels().next_threshold();
        const float frac = progression_->levels().at_max_level() || need <= 0.0
                               ? 1.0f
                               : static_cast<float>(std::min(
                                     1.0, progression_->levels().xp() / need));
        draw_rect(godot::Rect2(20.0f, 50.0f, 300.0f, 14.0f),
                  Color(0.08f, 0.15f, 0.10f, 1.0f), true);
        draw_rect(godot::Rect2(20.0f, 50.0f, 300.0f * frac, 14.0f),
                  Color(0.25f, 1.0f, 0.25f, 1.0f), true);
        draw_rect(godot::Rect2(20.0f, 50.0f, 300.0f, 14.0f),
                  Color(0.70f, 1.0f, 0.70f, 1.0f), false, 2.0f);
    }
    if (world_) {
        const Color coral(0.95f, 0.32f, 0.28f, 1.0f);
        const Color coral_edge(0.45f, 0.05f, 0.08f, 1.0f);
        for (const Enemy &e : world_->enemies()) {
            const Vector2 ep =
                center + Vector2(static_cast<float>(e.position.x),
                                 static_cast<float>(e.position.y));
            draw_circle(ep, static_cast<float>(e.radius), coral);
            draw_arc(ep, static_cast<float>(e.radius), 0.0f, 6.2831853f, 24,
                     coral_edge, 3.0f);
        }
        // Data-pool zones: translucent disc with a ring and tick-ripple.
        for (const Zone &z : world_->zones()) {
            const Vector2 zp =
                center + Vector2(static_cast<float>(z.position.x),
                                 static_cast<float>(z.position.y));
            const float fade = static_cast<float>(
                std::max(0.0, 1.0 - z.age / std::max(0.001, z.lifetime)));
            draw_circle(zp, static_cast<float>(z.radius),
                        Color(0.2f, 0.9f, 0.7f, 0.12f + 0.12f * fade));
            draw_arc(zp, static_cast<float>(z.radius), 0.0f, 6.2831853f, 48,
                     Color(0.4f, 1.0f, 0.85f, 0.8f), 3.0f);
            const double phase = std::fmod(z.age, z.tick_interval) / z.tick_interval;
            draw_arc(zp, static_cast<float>(z.radius * phase), 0.0f, 6.2831853f,
                     32, Color(0.7f, 1.0f, 0.95f, 0.5f * (1.0f - static_cast<float>(phase))),
                     2.0f);
        }
        // Tail sweep: expanding ring that fades over BurstVisual::kDuration.
        for (const BurstVisual &v : world_->bursts()) {
            const Vector2 vp =
                center + Vector2(static_cast<float>(v.center.x),
                                 static_cast<float>(v.center.y));
            const float t = static_cast<float>(v.age / BurstVisual::kDuration);
            draw_arc(vp, static_cast<float>(v.radius * (0.5 + 0.5 * t)), 0.0f,
                     6.2831853f, 48, Color(1.0f, 0.85f, 0.35f, 1.0f - t), 6.0f);
        }
        for (const Projectile &b : world_->projectiles()) {
            const Vector2 bp =
                center + Vector2(static_cast<float>(b.position.x),
                                 static_cast<float>(b.position.y));
            draw_circle(bp, static_cast<float>(b.radius),
                        Color(0.30f, 0.95f, 1.0f, 0.9f));
            draw_arc(bp, static_cast<float>(b.radius), 0.0f, 6.2831853f, 16,
                     Color(0.85f, 1.0f, 1.0f, 1.0f), 2.0f);
        }
    }

    const Vector2 p =
        center + Vector2(static_cast<float>(player_.position().x),
                         static_cast<float>(player_.position().y));
    if (has_pixels(active_sprite_)) {
        // Whole texture, aspect preserved, centred on the player. Mirrored when
        // facing left; the transform is reset right after drawing.
        const float w = static_cast<float>(active_sprite_->get_width());
        const float h = static_cast<float>(active_sprite_->get_height());
        const float draw_w = w * kPlayerSpriteHeight / h;
        const float mirror = facing_ < 0.0 ? -1.0f : 1.0f;
        draw_set_transform(p, 0.0f, Vector2(mirror, 1.0f));
        draw_texture_rect(active_sprite_,
                          godot::Rect2(-draw_w * 0.5f, -kPlayerSpriteHeight * 0.5f,
                                       draw_w, kPlayerSpriteHeight),
                          false);
        draw_set_transform(Vector2(0.0f, 0.0f), 0.0f, Vector2(1.0f, 1.0f));
        return;
    }
    // Procedural fish: only for characters with no sprite_asset configured.
    const float f = static_cast<float>(facing_);
    const Color deep(0.10f, 0.35f, 0.80f, 1.0f);
    const Color mid(0.20f, 0.55f, 0.95f, 1.0f);
    const Color belly(0.65f, 0.85f, 1.0f, 1.0f);

    // Forked tail and dorsal fin, then body, belly, eye.
    draw_circle(p + Vector2(-f * 40.0f, -13.0f), 13.0f, deep);
    draw_circle(p + Vector2(-f * 40.0f, 13.0f), 13.0f, deep);
    draw_circle(p + Vector2(-f * 32.0f, 0.0f), 11.0f, deep);
    draw_line(p + Vector2(-f * 6.0f, -22.0f), p + Vector2(f * 6.0f, -42.0f),
              deep, 9.0f);
    draw_line(p + Vector2(f * 6.0f, -42.0f), p + Vector2(f * 16.0f, -22.0f),
              deep, 7.0f);
    draw_ellipse(p, 36.0f, 28.0f, mid);
    draw_ellipse(p + Vector2(0.0f, 9.0f), 27.0f, 14.0f, belly);
    draw_circle(p + Vector2(f * 20.0f, -8.0f), 7.0f, Color(1, 1, 1, 1));
    draw_circle(p + Vector2(f * 22.0f, -8.0f), 3.5f, Color(0.02f, 0.05f, 0.12f, 1));
    draw_line(p + Vector2(f * 28.0f, 6.0f), p + Vector2(f * 20.0f, 9.0f),
              Color(0.02f, 0.05f, 0.12f, 1), 2.0f);
}

bool AISurvivorsBootstrap::run_native_checks() {
    const auto fail = [](const char *detail) {
        godot::UtilityFunctions::print(
            String("AI_SURVIVORS_INTEGRATION_FAILED: ") + String(detail));
        return false;
    };
    if (static_cast<Button *>(start_button_)->get_text() != String::utf8("开始游戏")) {
        return fail("menu button UTF-8 text corrupted");
    }
    if (!load_error_.empty()) {
        return fail(load_error_.c_str());
    }
    const CharacterDefinition *fish = registry_.find("deepseek_big_fish");
    if (fish == nullptr) {
        return fail("deepseek_big_fish not loaded from data/characters");
    }
    if (fish->max_hp != 100.0 || fish->move_speed != 220.0) {
        return fail("deepseek_big_fish numeric defaults wrong");
    }
    // The default character must have real art: configured, loaded, non-empty.
    const auto fish_art = art_.find(fish->id);
    if (fish->sprite_asset.empty() || fish->portrait_asset.empty() ||
        fish_art == art_.end() || !has_pixels(fish_art->second.sprite) ||
        !has_pixels(fish_art->second.portrait)) {
        return fail("deepseek_big_fish sprite or portrait texture not loaded");
    }

    GameStateManager states;
    PlayerController player(kArenaHalfWidth, kArenaHalfHeight, kPlayerRadius,
                            fish->move_speed);
    if (player.step(states.state(), 1.0, 1.0, 0.0) ||
        player.position().x != 0.0) {
        return fail("player moved outside Playing");
    }
    if (!states.start_run() || !player.step(states.state(), 0.5, 1.0, 0.0)) {
        return fail("player tick did not run while Playing");
    }
    if (std::abs(player.position().x - 110.0) > 1e-9) {
        return fail("player moved wrong distance in one tick");
    }
    if (!states.pause() || player.step(states.state(), 0.5, 1.0, 0.0) ||
        std::abs(player.position().x - 110.0) > 1e-9) {
        return fail("paused player was not frozen");
    }

    start_battle(fish->id);
    const bool in_battle = screen_ == Screen::kBattle &&
                           states_.state() == GameState::kPlaying &&
                           battle_label_->get_text().length() > 0;
    if (!in_battle || !world_ || world_->hp() != fish->max_hp) {
        on_back_to_menu_pressed();
        return fail("battle scene did not start for loaded character");
    }

    // Seeded ticks (10 s of fixed steps) must produce enemies and shots.
    bool saw_enemy = false;
    bool saw_shot = false;
    for (int i = 0; i < 600 && states_.state() == GameState::kPlaying; ++i) {
        world_->tick(states_, 1.0 / 60.0, player_.position());
        saw_enemy = saw_enemy || !world_->enemies().empty();
        saw_shot = saw_shot || !world_->projectiles().empty();
    }
    const bool combat_ok = saw_enemy && saw_shot;
    on_back_to_menu_pressed();
    if (!combat_ok) {
        return fail("seeded combat produced no enemies or projectiles");
    }

    // Lethal contact must move the run to the terminal Lost state.
    GameStateManager death_states;
    death_states.start_run();
    CombatStats fragile;
    fragile.max_hp = 1.0;
    CombatWorld doomed(fragile, 1);
    EnemySpawn killer;
    killer.position = Vec2{0.0, 0.0};
    killer.speed = 0.0;
    killer.contact_damage = 50.0;
    if (!doomed.spawn_enemy(killer) ||
        doomed.tick(death_states, 1.0 / 60.0, Vec2{}) !=
            CombatWorld::TickResult::kPlayerDied ||
        death_states.state() != GameState::kLost) {
        return fail("lethal contact did not reach the Lost state");
    }
    return true;
}

bool AISurvivorsBootstrap::run_progression_checks() {
    const auto fail = [this](const char *detail) {
        godot::UtilityFunctions::print(
            String("AI_SURVIVORS_INTEGRATION_FAILED: ") + String(detail));
        on_back_to_menu_pressed();
        return false;
    };
    const CharacterDefinition *fish = registry_.find("deepseek_big_fish");
    if (fish == nullptr) {
        return fail("progression check: character missing");
    }
    start_battle(fish->id);
    if (!progression_ || !world_ || states_.state() != GameState::kPlaying ||
        progression_->levels().level() != 1 ||
        progression_->inventory().level("bubble_shot") != 1) {
        return fail("progression did not start at level 1 with bubble_shot");
    }
    // A weak, harmless enemy inside weapon range; its orb lands inside the
    // pickup radius of the player standing nearby, so a real pickup levels up.
    player_.reset(Vec2{30.0, 0.0});
    EnemySpawn weak;
    weak.position = Vec2{100.0, 0.0};
    weak.hp = 1.0;
    weak.speed = 0.0;
    weak.contact_damage = 0.0;
    weak.xp_value = 10.0;
    if (!world_->spawn_enemy(weak)) {
        return fail("could not spawn progression test enemy");
    }
    for (int i = 0; i < 600 && states_.state() == GameState::kPlaying; ++i) {
        advance_frame(1.0 / 60.0);
    }
    if (states_.state() != GameState::kUpgrade ||
        progression_->levels().level() < 2 ||
        !progression_->cards().has_active_offer()) {
        return fail("real XP pickup did not open the upgrade choice");
    }
    const double frozen_time = timer_->elapsed_seconds();
    const double frozen_combat = world_->combat_time();
    advance_frame(1.0);
    if (timer_->elapsed_seconds() != frozen_time ||
        world_->combat_time() != frozen_combat) {
        return fail("world or survival time advanced during upgrade");
    }
    const UpgradeOffer offer = progression_->cards().current_offer();
    const PlayerRuntimeStats before = progression_->stats();
    const int pending = progression_->levels().pending_levels();
    on_card_pressed(to_godot(offer.card_ids[0]),
                    static_cast<int64_t>(offer.generation) + 1);
    if (states_.state() != GameState::kUpgrade ||
        progression_->levels().pending_levels() != pending) {
        return fail("stale card callback mutated state");
    }
    on_card_pressed(to_godot(offer.card_ids[0]),
                    static_cast<int64_t>(offer.generation));
    const PlayerRuntimeStats &after = progression_->stats();
    const bool changed =
        after.max_hp != before.max_hp || after.armor != before.armor ||
        after.damage_multiplier != before.damage_multiplier ||
        after.attack_speed_multiplier != before.attack_speed_multiplier ||
        after.move_speed != before.move_speed ||
        after.crit_chance != before.crit_chance ||
        after.pickup_radius != before.pickup_radius ||
        progression_->inventory().level("bubble_shot") > 1 ||
        progression_->inventory().entries().size() > 1;
    if (!changed || progression_->levels().pending_levels() != pending - 1) {
        return fail("valid card did not change stats or consume a level");
    }
    if (world_->max_hp() != after.max_hp || world_->stats().armor != after.armor ||
        world_->stats().damage_multiplier != after.damage_multiplier ||
        player_.move_speed() != after.move_speed) {
        return fail("card was not applied to world/player");
    }
    if (pending - 1 > 0 && states_.state() != GameState::kUpgrade) {
        return fail("queued levels did not keep the Upgrade state");
    }
    for (int guard = 0; states_.state() == GameState::kUpgrade; ++guard) {
        if (guard > 1000 || !progression_->cards().has_active_offer()) {
            return fail("upgrade flow did not terminate");
        }
        const UpgradeOffer next = progression_->cards().current_offer();
        on_card_pressed(to_godot(next.card_ids[0]),
                        static_cast<int64_t>(next.generation));
    }
    advance_frame(1.0 / 60.0);
    if (timer_->elapsed_seconds() <= frozen_time) {
        return fail("survival time did not resume after upgrade");
    }
    start_battle(fish->id); // restart clears orbs, levels, cards, inventory
    const bool clean = progression_ && progression_->levels().level() == 1 &&
                       progression_->experience().orbs().empty() &&
                       progression_->inventory().level("bubble_shot") == 1 &&
                       progression_->cards().rank("attr_armor") == 0;
    on_back_to_menu_pressed();
    if (!clean) {
        return fail("restart did not clear progression");
    }
    return true;
}

bool AISurvivorsBootstrap::run_weapon_checks(bool keep_scene) {
    const auto fail = [this](const char *detail) {
        godot::UtilityFunctions::print(
            String("AI_SURVIVORS_INTEGRATION_FAILED: ") + String(detail));
        on_back_to_menu_pressed();
        return false;
    };
    start_battle("deepseek_big_fish");
    if (!world_ || !progression_) return fail("weapon fixture did not start");
    // Internal integration fixture: real generated offers and native button
    // callbacks. Each grant earns exactly one choice; no inventory injection.
    const auto choose_wanted = [this](const std::string &wanted) {
        const std::string acquire_prefix = "weapon_acquire_";
        if (wanted.rfind(acquire_prefix, 0) == 0 &&
            progression_->inventory().has(wanted.substr(acquire_prefix.size()))) {
            return true; // may have been chosen while seeking another offer
        }
        for (int attempt = 0; attempt < 128; ++attempt) {
            if (states_.state() != GameState::kPlaying) return false;
            const double needed = progression_->levels().next_threshold() -
                                  progression_->levels().xp();
            progression_->grant_xp(GameState::kPlaying, needed /
                                      progression_->stats().xp_multiplier);
            present_upgrade(prepare_next_upgrade(states_, *progression_));
            if (!progression_->cards().has_active_offer()) return false;
            const UpgradeOffer offer = progression_->cards().current_offer();
            const auto found = std::find(offer.card_ids.begin(),
                                         offer.card_ids.end(), wanted);
            const std::string pick = found != offer.card_ids.end()
                                         ? *found : offer.card_ids.front();
            on_card_pressed(to_godot(pick), static_cast<int64_t>(offer.generation));
            if (found != offer.card_ids.end()) return true;
        }
        return false;
    };
    if (!choose_wanted("weapon_acquire_tail_sweep") ||
        !progression_->inventory().has("tail_sweep")) {
        return fail("native card callback did not acquire tail sweep");
    }
    CombatConfig config;
    config.waves_enabled = false;
    world_ = std::make_unique<CombatWorld>(progression_->stats().combat(), 13, config);
    if (!initialize_run_loadout(*world_, *progression_).empty()) {
        return fail("acquired loadout did not initialize");
    }
    EnemySpawn target;
    target.position = Vec2{100.0, 0.0};
    target.hp = 1000.0;
    target.speed = 0.0;
    target.contact_damage = 0.0;
    target.xp_value = 0.0;
    world_->spawn_enemy(target);
    target.position = Vec2{-100.0, 0.0};
    target.hp = 1.0;
    world_->spawn_enemy(target);
    advance_frame(1.0 / 60.0);
    if (world_->kill_count() != 1 || world_->bursts().empty() ||
        world_->enemies().size() != 1 || world_->enemies()[0].hp >= 1000.0) {
        return fail("acquired sweep did not damage and kill actual enemies");
    }
    double before_damage = 0.0;
    double before_fraction = 0.0;
    for (const WeaponManager::Slot &slot : world_->weapon_slots()) {
        if (slot.spec.id == "tail_sweep") {
            before_damage = slot.spec.damage;
            before_fraction = slot.cooldown / slot.interval;
        }
    }
    const double enemy_hp = world_->enemies()[0].hp;
    if (!choose_wanted("weapon_upgrade_tail_sweep")) {
        return fail("native callback did not offer sweep upgrade");
    }
    bool upgraded = false;
    for (const WeaponManager::Slot &slot : world_->weapon_slots()) {
        if (slot.spec.id == "tail_sweep") {
            upgraded = slot.spec.damage > before_damage &&
                       std::abs(slot.cooldown / slot.interval - before_fraction) < 1e-8;
        }
    }
    if (!upgraded || world_->kill_count() != 1 ||
        world_->enemies()[0].hp != enemy_hp) {
        return fail("native upgrade reset combat or did not update damage");
    }
    if (!choose_wanted("weapon_acquire_data_pool")) {
        return fail("native callback did not acquire data pool");
    }
    advance_frame(1.0 / 60.0);
    if (world_->zones().empty()) return fail("acquired pool did not create a real zone");
    refresh_battle_ui();
    if (keep_scene) {
        set_physics_process(false); // screenshot fixture only
        return true;
    }
    on_back_to_menu_pressed();
    return true;
}

bool AISurvivorsBootstrap::run_core_checks() {
    const auto fail = [](const char *detail) {
        godot::UtilityFunctions::print(String("AI_SURVIVORS_INTEGRATION_FAILED: ") +
                     String(detail));
        return false;
    };

    GameStateManager states;
    GameTimer timer(states);

    if (states.state() != GameState::kMenu) {
        return fail("initial state is not Menu");
    }
    if (!states.start_run()) {
        return fail("could not start run");
    }
    if (timer.advance(10.0) != GameTimer::AdvanceResult::kAdvanced) {
        return fail("first playing tick did not advance");
    }
    if (!states.pause()) {
        return fail("could not pause");
    }
    if (timer.advance(5.0) != GameTimer::AdvanceResult::kFrozen) {
        return fail("paused clock was not frozen");
    }
    if (timer.elapsed_seconds() != 10.0) {
        return fail("paused clock changed value");
    }
    if (!states.resume()) {
        return fail("could not resume");
    }
    if (!states.begin_upgrade()) {
        return fail("could not begin upgrade");
    }
    if (timer.advance(100.0) != GameTimer::AdvanceResult::kFrozen) {
        return fail("upgrade clock was not frozen");
    }
    if (timer.elapsed_seconds() != 10.0) {
        return fail("upgrade clock changed value");
    }
    if (!states.complete_upgrade()) {
        return fail("could not complete upgrade");
    }
    if (timer.advance(1190.0) != GameTimer::AdvanceResult::kWinReached) {
        return fail("exact 1200 second boundary did not win");
    }
    if (states.state() != GameState::kWon) {
        return fail("state is not Won at boundary");
    }
    if (timer.elapsed_seconds() != 1200.0) {
        return fail("elapsed clock is not exactly 1200");
    }
    if (timer.advance(1.0) != GameTimer::AdvanceResult::kFrozen) {
        return fail("terminal clock was not frozen");
    }
    if (states.pause() || states.lose() || states.start_run()) {
        return fail("terminal state accepted a transition");
    }
    if (!timer.restart()) {
        return fail("explicit restart failed");
    }
    if (states.state() != GameState::kMenu ||
        timer.elapsed_seconds() != 0.0) {
        return fail("restart did not reset state and clock");
    }
    if (timer.advance(-1.0) != GameTimer::AdvanceResult::kRejected) {
        return fail("negative delta was not rejected");
    }

    // Accelerated deterministic 20 minute simulation: 240000 * 5 ms steps.
    if (!states.start_run()) {
        return fail("could not start accelerated simulation");
    }
    constexpr int kTotalTicks = 240000;
    int advanced_ticks = 0;
    int win_ticks = 0;
    for (int tick = 0; tick < kTotalTicks; ++tick) {
        const GameTimer::AdvanceResult result = timer.advance(0.005);
        if (result == GameTimer::AdvanceResult::kAdvanced) {
            ++advanced_ticks;
        } else if (result == GameTimer::AdvanceResult::kWinReached) {
            ++win_ticks;
        } else {
            return fail("accelerated simulation froze or rejected a tick");
        }
    }
    if (advanced_ticks != kTotalTicks - 1 || win_ticks != 1) {
        return fail("accelerated simulation had wrong tick counts");
    }
    if (states.state() != GameState::kWon ||
        timer.elapsed_seconds() != 1200.0) {
        return fail("accelerated simulation did not end at 1200 seconds");
    }

    return true;
}

void AISurvivorsBootstrap::_ready() {
    build_ui();
    load_characters();
    populate_character_buttons();
    if (!load_error_.empty()) {
        error_label_->set_text(to_godot("角色配置错误，无法开始：\n" + load_error_));
        start_button_->set_visible(false);
    }
    show_screen(Screen::kMenu);

    bool integration_test = false;
    bool visual_battle = false;
    bool visual_select = false;
    bool visual_pause = false;
    bool visual_upgrade = false;
    bool visual_weapons = false;
    const PackedStringArray args =
        OS::get_singleton()->get_cmdline_user_args();
    for (int64_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--integration-test") {
            integration_test = true;
        } else if (args[i] == "--visual-battle") {
            visual_battle = true;
        } else if (args[i] == "--visual-select") {
            visual_select = true;
        } else if (args[i] == "--visual-upgrade") {
            visual_battle = true;
            visual_upgrade = true;
        } else if (args[i] == "--visual-weapons") {
            visual_weapons = true;
        } else if (args[i] == "--visual-pause") {
            visual_battle = true;
            visual_pause = true;
        }
    }

    // Headless simulation: no battle scene, no frame loop. It runs on the
    // character data loaded above and quits with the report's exit code.
    SimulationRequest simulation;
    std::string simulation_error;
    if (!parse_simulation_args(args, simulation, simulation_error)) {
        godot::UtilityFunctions::push_error(
            to_godot("argument error: " + simulation_error));
        get_tree()->quit(1);
        return;
    }
    if (simulation.requested) {
        if (!load_error_.empty()) {
            godot::UtilityFunctions::push_error(
                to_godot("simulation aborted: character data did not load"));
            get_tree()->quit(1);
            return;
        }
        get_tree()->quit(run_simulation_report(registry_, simulation));
        return;
    }

    if (!integration_test) {
        // Normal scene stays open until the window is closed.
        if (visual_weapons && load_error_.empty()) {
            if (!run_weapon_checks(true)) get_tree()->quit(1);
        } else if (visual_battle && load_error_.empty()) {
            start_battle(registry_.characters().front().id);
            // Deterministic warm-up so the screenshot shows a populated arena.
            if (world_) simulate_battle(1.0);
            if (visual_pause) { states_.pause(); show_screen(Screen::kBattle); }
            if (visual_upgrade && progression_ &&
                states_.state() == GameState::kPlaying) {
                // Screenshot only: a legitimate XP grant through the normal
                // level/offer pipeline; normal mode has no such shortcut.
                progression_->grant_xp(GameState::kPlaying, 25.0);
                present_upgrade(prepare_next_upgrade(states_, *progression_));
                refresh_battle_ui();
            }
        } else if (visual_select && load_error_.empty()) {
            on_start_pressed();
        }
        return;
    }

    if (get_class() != StringName("AISurvivorsBootstrap")) {
        godot::UtilityFunctions::print("AI_SURVIVORS_INTEGRATION_FAILED: native class mismatch");
        get_tree()->quit(1);
        return;
    }
    if (!run_core_checks()) {
        get_tree()->quit(1);
        return;
    }
    if (!run_native_checks()) {
        get_tree()->quit(1);
        return;
    }
    if (!run_progression_checks()) {
        get_tree()->quit(1);
        return;
    }
    if (!run_weapon_checks()) {
        get_tree()->quit(1);
        return;
    }
    if (status_label_ == nullptr || !status_label_->is_inside_tree() ||
        status_label_->get_text().length() == 0) {
        godot::UtilityFunctions::print("AI_SURVIVORS_INTEGRATION_FAILED: native label UI missing");
        get_tree()->quit(1);
        return;
    }

    godot::UtilityFunctions::print("AI_SURVIVORS_INTEGRATION_OK");
    get_tree()->quit(0);
}

} // namespace ai_survivors
