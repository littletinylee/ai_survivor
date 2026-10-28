#include "gameplay/ai_survivors_bootstrap.h"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "core/game_state.h"
#include "core/game_timer.h"

namespace ai_survivors {

using godot::Color;
using godot::ColorRect;
using godot::Control;
using godot::Label;
using godot::OS;
using godot::PackedStringArray;
using godot::String;
using godot::StringName;

void AISurvivorsBootstrap::_bind_methods() {}

void AISurvivorsBootstrap::build_ui() {
    Control *ui_root = memnew(Control);
    ui_root->set_name("AISurvivorsUiRoot");
    ui_root->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    add_child(ui_root);
    ui_root->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

    ColorRect *background = memnew(ColorRect);
    background->set_name("Background");
    background->set_color(Color(0.035f, 0.045f, 0.075f, 1.0f));
    background->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    ui_root->add_child(background);
    background->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

    Label *label = memnew(Label);
    label->set_name("NativeStatusLabel");
    label->set_horizontal_alignment(godot::HORIZONTAL_ALIGNMENT_CENTER);
    label->set_vertical_alignment(godot::VERTICAL_ALIGNMENT_CENTER);
    label->add_theme_font_size_override("font_size", 30);
    label->add_theme_color_override("font_color",
                                    Color(0.90f, 0.95f, 1.0f, 1.0f));
    label->set_text(
        "AI SURVIVORS\n"
        "\n"
        "Phase 0 - C++ GDExtension bootstrap\n"
        "Native class: AISurvivorsBootstrap\n"
        "Core: explicit state machine + 1200 s survival clock\n"
        "\n"
        "Close the window to exit.");
    ui_root->add_child(label);
    label->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

    status_label_ = label;
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

    bool integration_test = false;
    const PackedStringArray args =
        OS::get_singleton()->get_cmdline_user_args();
    for (int64_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--integration-test") {
            integration_test = true;
            break;
        }
    }

    if (!integration_test) {
        // Normal scene stays open until the window is closed.
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
