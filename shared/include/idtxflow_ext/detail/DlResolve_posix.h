#pragma once
/**
 * @file DlResolve_posix.h
 * @brief POSIX runtime validation that IDTXFlow symbols are resolvable.
 *
 * On Linux and macOS, Godot loads the IDTXFlow shared library before dependent
 * extensions initialize (via the .gdextension [dependencies] section). Unlike
 * Windows, there is no delay-load mechanism — the dynamic linker resolves
 * symbols at load time via RPATH/RUNPATH.
 *
 * This helper performs a lightweight runtime check using dlsym(RTLD_DEFAULT)
 * to confirm that key IDTXFlow symbols are actually available in the process.
 * If they are not, it prints a clear diagnostic message.
 *
 * DO NOT include this header directly — it is pulled in by ExtensionBootstrap.cpp.
 */

#ifndef _WIN32

#include <dlfcn.h>
#include <cstdio>

namespace idtxflow_ext
{
namespace detail
{

/**
 * Verify that the IDTXFlow shared library is loaded and its symbols are
 * accessible to this extension.
 *
 * @return 0 on success, non-zero if a required symbol cannot be found.
 */
inline int validateIdtxflowLoaded()
{
    // We probe for a well-known C symbol exported by the IDTXFlow Godot
    // GDExtension.  The symbol name "idtxflow_library_init" is the
    // GDExtension entry-point of the base IDTXFlow library.
    const char* probeSymbol = "idtxflow_library_init";

    void* sym = dlsym(RTLD_DEFAULT, probeSymbol);
    if (sym != nullptr)
    {
        return 0; // IDTXFlow is loaded and symbols are resolvable
    }

    fprintf(stderr,
            "[idtxflow_ext] FATAL: Cannot find symbol '%s' in loaded libraries.\n"
            "  The IDTXFlow base GDExtension must be present at res://addons/IDTXFlow/\n"
            "  so that Godot loads the shared library before this extension initializes.\n"
            "  dlerror: %s\n",
            probeSymbol, dlerror() ? dlerror() : "(none)");

    return 1;
}

} // namespace detail
} // namespace idtxflow_ext

#endif // !_WIN32