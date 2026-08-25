#pragma once

/**
 * DLL export/import macro for the engine-agnostic IDTXFlow API.
 * IDTXFlow is currently built by the Godot GDExtension target, so it uses the
 * same export define while keeping engine-agnostic headers independent of the
 * Godot-specific include tree.
 */
#if defined(_WIN32) || defined(_WIN64)
    #ifdef IDTXFLOW_GODOT_EXPORTS
        #define IDTXFLOW_API __declspec(dllexport)
    #else
        #define IDTXFLOW_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define IDTXFLOW_API __attribute__((visibility("default")))
#else
    #define IDTXFLOW_API
#endif
