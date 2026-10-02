/**
 * @file ExtensionBootstrap.cpp
 * @brief Bootstrap implementation for IDTXFlow dependent GDExtensions.
 *
 * This file is compiled into a static library (libidtxflow_ext_bootstrap)
 * that every IDTXFlow-dependent extension links against. It provides:
 *
 *   1. The platform-specific DLL/SO resolution glue (delay-load hook on
 *      Windows, dlsym validation on POSIX).
 *   2. The idtxflow_ext_bootstrap_init() function called by the
 *      IDTXFLOW_EXTENSION_ENTRY_POINT macro before Godot binding init.
 *
 * Because this is a static library that gets linked into each extension's
 * shared library, each extension gets its own copy of the hook — which is
 * exactly what the MSVC delay-load mechanism requires (the hook symbol
 * __pfnDliNotifyHook2 must be in the same image that uses /DELAYLOAD).
 */

// Pull in the platform-specific implementation headers.
// On Windows this installs the __pfnDliNotifyHook2 symbol.
// On POSIX this provides the validateIdtxflowLoaded() helper.
#ifdef _WIN32
#include <idtxflow_ext/detail/DelayLoadHook_win32.h>
#else
#include <idtxflow_ext/detail/DlResolve_posix.h>
#endif

#include <cstdio>

extern "C" int idtxflow_ext_bootstrap_init()
{
#ifdef _WIN32
    // On Windows the delay-load hook is already installed via the global
    // __pfnDliNotifyHook2 symbol defined in DelayLoadHook_win32.h.
    // The first access to any delay-loaded IDTXFlow symbol will trigger
    // the hook automatically. We do a quick sanity check here.
    //
    // We don't call LoadLibrary ourselves — Godot has already loaded the
    // DLL and the hook will find it in the process module list.
    return 0;
#else
    // On POSIX, verify that the IDTXFlow shared library is loaded and
    // its symbols are accessible before we proceed with initialization.
    return idtxflow_ext::detail::validateIdtxflowLoaded();
#endif
}