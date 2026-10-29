#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "gameplay/ai_survivors_bootstrap.h"

namespace ai_survivors {
namespace {

void initialize_ai_survivors_module(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(AISurvivorsBootstrap);
}

void uninitialize_ai_survivors_module(godot::ModuleInitializationLevel) {}

} // namespace
} // namespace ai_survivors

extern "C" {

GDExtensionBool GDE_EXPORT ai_survivors_library_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(
        p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(
        ai_survivors::initialize_ai_survivors_module);
    init_obj.register_terminator(
        ai_survivors::uninitialize_ai_survivors_module);
    init_obj.set_minimum_library_initialization_level(
        godot::MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
