/**
 * @file register_types.cpp
 * @brief GDExtension entry point for the IDTXFlow extension plugin template.
 *
 * This file demonstrates the three extension mechanisms:
 *   1. Registering custom Godot node classes (GDREGISTER_CLASS)
 *   2. Registering custom USD prim converters into IDTXFlow's shared registry
 *   3. Cleaning up on shutdown
 *
 * Replace the example converter with your own implementations.
 */
#include "register_types.h"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

// Bootstrapping
#include <idtxflow_ext/ExtensionBootstrap.h>

// IDTXFlow public API – Godot-specific (DLL-safe singleton)
#include <idtxflow_godot/PrimConverterRegistryGodot.h>
#include <idtxflow_godot/version.h>

// Your custom converters
#include "converters/ExamplePrimConverter.h"

// Your custom nodes
#include "nodes/ExampleUsdNode3D.h"

using Registry = idtxflow::converter::PrimConverterRegistry<idtxflow::types::TargetEngineGodot>;

using namespace godot;


void initialize_my_extension_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    // -- Version check (optional but recommended) --
    #if IDTXFLOW_GODOT_VERSION < IDTXFLOW_GODOT_MAKE_VERSION(0, 4, 0)
    #error "This extension requires IDTXFlow Godot API >= 0.4.0"
    #endif
    
    GDREGISTER_CLASS(ExampleUsdNode3D)
        
    // Register prim converters into IDTXFlow's shared registry
    Registry::Instance().Register(std::make_shared<ExamplePrimConverter>());
    
    print_verbose("MyIdtxflowExtension initialized");
}

void uninitialize_my_extension_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    
    Registry::Instance().Unregister("ExamplePrimConverter");
    
    print_verbose("MyIdtxflowExtension uninitialized");
}

// Generate the GDExtension C entry-point with automatic IDTXFlow DLL bootstrap.
// The macro handles platform-specific delay-load hooks (Windows) and dlsym
// validation (POSIX) so no manual glue code is needed here.
IDTXFLOW_EXTENSION_ENTRY_POINT(
    my_extension_library_init,
    initialize_my_extension_module,
    uninitialize_my_extension_module
)
