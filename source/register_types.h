#pragma once

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

/**
 * @brief Initialize the extension
 * Called when the GDExtension is loaded
 */
void initialize_idtxflow_module(ModuleInitializationLevel p_level);

/**
 * @brief Cleanup the extension
 * Called when the GDExtension is unloaded
 */
void uninitialize_idtxflow_module(ModuleInitializationLevel p_level);

/**
 * @brief GDExtension entry point
 * Required function for GDExtension initialization
 */
extern "C"
{
    GDExtensionBool GDE_EXPORT idtxflow_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                     const GDExtensionClassLibraryPtr p_library,
                                                     GDExtensionInitialization* r_initialization);
}
