#pragma once
/**
 * @file DelayLoadHook_win32.h
 * @brief Windows delay-load notification hook for IDTXFlow dependent extensions.
 *
 * This header-only implementation is compiled into the bootstrap static library.
 * It intercepts the MSVC delay-load mechanism so that when the extension first
 * accesses an IDTXFlow symbol, the hook resolves the DLL from the already-loaded
 * module list (Godot pre-loads it via the .gdextension [dependencies] section).
 *
 * DO NOT include this header directly — it is pulled in by ExtensionBootstrap.cpp.
 */

#ifdef _WIN32

#include <Windows.h>
#include <delayimp.h>
#include <Psapi.h>
#include <cstdio>

#pragma comment(lib, "Psapi.lib")

namespace idtxflow_ext
{
namespace detail
{

/**
 * Search all loaded modules by base filename (case-insensitive).
 *
 * Godot on Windows renames loaded GDExtension DLLs by prepending '~' to the
 * filename (to avoid file-locking issues). We strip leading '~' characters
 * before comparing so that "~libfoo.dll" matches a request for "libfoo.dll".
 */
inline HMODULE findLoadedModuleByName(const char* dllName)
{
    HANDLE hProcess = GetCurrentProcess();
    HMODULE modules[2048];
    DWORD cbNeeded = 0;

    if (!EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded)) return NULL;

    // Strip leading '~' from the requested name
    while (dllName[0] == '~')
        dllName++;

    DWORD count = cbNeeded / sizeof(HMODULE);
    for (DWORD i = 0; i < count; i++)
    {
        char modPath[MAX_PATH];
        if (GetModuleFileNameA(modules[i], modPath, sizeof(modPath)))
        {
            const char* baseName = strrchr(modPath, '\\');
            if (!baseName) baseName = strrchr(modPath, '/');
            baseName = baseName ? baseName + 1 : modPath;

            // Strip Godot's '~' prefix from the loaded module name
            while (baseName[0] == '~')
                baseName++;

            if (_stricmp(baseName, dllName) == 0) return modules[i];
        }
    }
    return NULL;
}

/** Dump all loaded module paths to stderr for diagnostics. */
inline void dumpLoadedModules()
{
    HANDLE hProcess = GetCurrentProcess();
    HMODULE modules[2048];
    DWORD cbNeeded = 0;

    fprintf(stderr, "[idtxflow_ext] Currently loaded modules:\n");
    if (EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded))
    {
        DWORD count = cbNeeded / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++)
        {
            char modPath[MAX_PATH];
            if (GetModuleFileNameA(modules[i], modPath, sizeof(modPath)))
            {
                fprintf(stderr, "  [%4lu] %s\n", (unsigned long)i, modPath);
            }
        }
    }
    else
    {
        fprintf(stderr, "  (EnumProcessModules failed, error %lu)\n", (unsigned long)GetLastError());
    }
}

/**
 * Delay-load notification hook.
 *
 * On dliNotePreLoadLibrary, resolves the DLL from the already-loaded module
 * list. Falls back to enumerating all process modules if GetModuleHandleA
 * fails (which happens when Godot loaded the DLL via a full path).
 */
inline FARPROC WINAPI delayLoadNotifyHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (dliNotify == dliNotePreLoadLibrary)
    {
        // Fast path: standard Win32 module lookup by base name
        HMODULE hMod = GetModuleHandleA(pdli->szDll);
        if (hMod) return (FARPROC)hMod;

        // Fallback: enumerate all loaded modules
        hMod = findLoadedModuleByName(pdli->szDll);
        if (hMod)
        {
            OutputDebugStringA("[idtxflow_ext] Resolved '");
            OutputDebugStringA(pdli->szDll);
            OutputDebugStringA("' via module enumeration fallback.\n");
            return (FARPROC)hMod;
        }

        // Neither method found the module — dump diagnostics
        OutputDebugStringA("[idtxflow_ext] ERROR: delay-load failed for '");
        OutputDebugStringA(pdli->szDll);
        OutputDebugStringA("'. Is the IDTXFlow base GDExtension installed?\n");

        fprintf(stderr,
                "[idtxflow_ext] FATAL: Cannot resolve delay-loaded dependency '%s'.\n"
                "  The IDTXFlow base GDExtension must be present at res://addons/IDTXFlow/\n"
                "  so that Godot pre-loads the DLL before this extension initializes.\n",
                pdli->szDll);

        dumpLoadedModules();
    }
    return NULL;
}

} // namespace detail
} // namespace idtxflow_ext

// Install the hook at the global level. The MSVC delay-load helper looks for
// this well-known symbol to dispatch notifications.
extern "C" const PfnDliHook __pfnDliNotifyHook2 = idtxflow_ext::detail::delayLoadNotifyHook;

#endif // _WIN32