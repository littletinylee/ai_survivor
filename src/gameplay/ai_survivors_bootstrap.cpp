#include "gameplay/ai_survivors_bootstrap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
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

} // namespace

void AISurvivorsBootstrap::_bind_methods() {}

void AISurvivorsBootstrap::load_characters() {
    registry_ = CharacterRegistry();
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
    if (!load_error_.empty()) {
        godot::UtilityFunctions::push_error(
            to_godot("Character load failed: " + load_error_));
    }
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
        button->set_custom_minimum_size(Vector2(600, 110));
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
    clear_card_buttons();
    progression_ = std::move(session);
    active_character_ = character;
    // Run-local state is recreated for each battle.
    states_ = GameStateManager();
    timer_ = std::make_unique<GameTimer>(states_);
    states_.start_run();
    player_.reset();
    player_.set_move_speed(progression_->stats().move_speed);
    // Fixed base seed plus a per-session run counter: reproducible, yet
    // restarts are not identical copies.
    world_ = std::make_unique<CombatWorld>(CombatStats::from_character(*character),
                                           0xA15D0000ULL + run_counter_++);
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
    }
}

void AISurvivorsBootstrap::advance_frame(double delta) {
    if (states_.state() != GameState::kPlaying || !world_ || !timer_ ||
        !progression_) {
        return;
    }
    // Death has priority over victory: combat runs before the single
    // survival clock, which only advances if the run is still Playing.
    world_->tick(states_, delta, player_.position());
    if (states_.state() == GameState::kPlaying) {
        timer_->advance(delta);
    }
    if (states_.state() == GameState::kPlaying) {
        progression_->ingest_and_collect(states_.state(),
                                         world_->take_kill_events(),
                                         player_.position());
        if (progression_->needs_upgrade()) {
            begin_upgrade_flow();
        }
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

void AISurvivorsBootstrap::begin_upgrade_flow() {
    if (!progression_ || !states_.begin_upgrade()) {
        return;
    }
    continue_upgrade_flow();
}

void AISurvivorsBootstrap::continue_upgrade_flow() {
    clear_card_buttons();
    if (progression_->advance_offer()) {
        show_offer();
    } else if (states_.state() == GameState::kUpgrade) {
        states_.complete_upgrade();
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

void AISurvivorsBootstrap::apply_runtime_stats() {
    if (!progression_ || !world_) {
        return;
    }
    const PlayerRuntimeStats &s = progression_->stats();
    world_->update_player_stats(s.combat(), progression_->weapon_damage_scale());
    player_.set_move_speed(s.move_speed);
}

void AISurvivorsBootstrap::on_card_pressed(const String &card_id,
                                           int64_t generation) {
    if (!progression_ || states_.state() != GameState::kUpgrade) {
        return;
    }
    // Stale / repeated / invalid callbacks are rejected without mutation.
    if (progression_->choose(static_cast<std::uint64_t>(generation),
                             to_std(card_id)) != ChooseResult::kApplied) {
        return;
    }
    apply_runtime_stats();
    continue_upgrade_flow();
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
        if (player_.step(states_.state(), delta, ix, iy) && ix != 0.0) {
            facing_ = ix > 0.0 ? 1.0 : -1.0;
        }
        advance_frame(delta);
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
        progression_->inventory().level("bubble_shot") > 1;
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
        } else if (args[i] == "--visual-pause") {
            visual_battle = true;
            visual_pause = true;
        }
    }

    if (!integration_test) {
        // Normal scene stays open until the window is closed.
        if (visual_battle && load_error_.empty()) {
            start_battle(registry_.characters().front().id);
            // Deterministic warm-up so the screenshot shows a populated arena.
            if (world_) simulate_battle(1.0);
            if (visual_pause) { states_.pause(); show_screen(Screen::kBattle); }
            if (visual_upgrade && progression_ &&
                states_.state() == GameState::kPlaying) {
                // Screenshot only: a legitimate XP grant through the normal
                // level/offer pipeline; normal mode has no such shortcut.
                progression_->grant_xp(GameState::kPlaying, 25.0);
                if (progression_->needs_upgrade()) begin_upgrade_flow();
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
