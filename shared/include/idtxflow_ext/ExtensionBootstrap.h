#pragma once
/**
 * @file ExtensionBootstrap.h
 * @brief Zero-boilerplate bootstrap for GDExtensions that depend on IDTXFlow.
 *
 * This header provides:
 *   1. Automatic platform-specific DLL resolution (delay-load on Windows,
 *      dlopen validation on POSIX) so that the extension can link against
 *      libidtxflow symbols without manual hooking code.
 *   2. A convenience macro IDTXFLOW_EXTENSION_ENTRY_POINT() that generates
 *      the GDExtension C entry-point function.
 *
 * Usage in your register_types.cpp:
 *
 *   #include <idtxflow_ext/ExtensionBootstrap.h>
 *
 *   void my_init(godot::ModuleInitializationLevel level) {
 *       if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
 *           GDREGISTER_CLASS(MyNode)
 *       }
 *   }
 *   void my_deinit(godot::ModuleInitializationLevel level) { }
 *
 *   IDTXFLOW_EXTENSION_ENTRY_POINT(
 *       myextension_library_init,
 *       my_init,
 *       my_deinit
 *   )
 *
 * The bootstrap static library (libidtxflow_ext_bootstrap) must be linked
 * into your extension. The IDTXFlow SCons build tools handle this automatically.
 */

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Ensure the IDTXFlow DLL symbols are resolvable before any are accessed.
     *
     * On Windows: validates the delay-load hook is active and can resolve
     *             the IDTXFlow DLL from the already-loaded module list.
     * On POSIX:   verifies that key IDTXFlow symbols are accessible via
     *             dlsym(RTLD_DEFAULT, ...).
     *
     * @return 0 on success, non-zero on failure.
     */
    int idtxflow_ext_bootstrap_init();

#ifdef __cplusplus
}
#endif

/**
 * Macro that generates the GDExtension C entry-point function with automatic
 * bootstrap initialization.
 *
 * @param entry_symbol   The C function name (must match .gdextension entry_symbol)
 * @param init_func      void(godot::ModuleInitializationLevel) — your initializer
 * @param deinit_func    void(godot::ModuleInitializationLevel) — your terminator
 */
#define IDTXFLOW_EXTENSION_ENTRY_POINT(entry_symbol, init_func, deinit_func)                                           \
    extern "C"                                                                                                         \
    {                                                                                                                  \
        GDExtensionBool GDE_EXPORT entry_symbol(GDExtensionInterfaceGetProcAddress p_get_proc_address,                 \
                                                const GDExtensionClassLibraryPtr p_library,                            \
                                                GDExtensionInitialization* r_initialization)                           \
        {                                                                                                              \
            if (idtxflow_ext_bootstrap_init() != 0)                                                                    \
            {                                                                                                          \
                return false;                                                                                          \
            }                                                                                                          \
            godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);           \
            init_obj.register_initializer(init_func);                                                                  \
            init_obj.register_terminator(deinit_func);                                                                 \
            init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);               \
            return init_obj.init();                                                                                    \
        }                                                                                                              \
    }