#pragma once

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>

namespace ai_survivors {

// Minimal Phase 0 scene-layer bootstrap.
//
// The node proves that the native GDExtension class is registered, builds a
// simple native Label UI, and exposes an opt-in headless integration test.
// The normal scene never quits; only the explicit --integration-test user
// argument makes it exit with a success/failure code.
class AISurvivorsBootstrap : public godot::Node {
    GDCLASS(AISurvivorsBootstrap, godot::Node)

public:
    AISurvivorsBootstrap() = default;
    ~AISurvivorsBootstrap() override = default;

    void _ready() override;

protected:
    static void _bind_methods();

private:
    void build_ui();
    bool run_core_checks();

    godot::Label *status_label_ = nullptr;
};

} // namespace ai_survivors
