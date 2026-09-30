#pragma once
/**
 * @file idtxflow_godot_api.h
 * @brief DLL export/import macro for the IDTXFlow Godot binding layer.
 *
 * This is the public API header for third-party GDExtension plugins that depend
 * on IDTXFlow. It defines IDTXFLOW_GODOT_API which controls symbol visibility.
 *
 * When building IDTXFlow itself, IDTXFLOW_GODOT_EXPORTS is defined and the macro
 * expands to dllexport. When consumed by a dependent extension, it expands to dllimport.
 */

#if defined(_WIN32) || defined(_WIN64)
#ifdef IDTXFLOW_GODOT_EXPORTS
#define IDTXFLOW_GODOT_API __declspec(dllexport)
#else
#define IDTXFLOW_GODOT_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define IDTXFLOW_GODOT_API __attribute__((visibility("default")))
#else
#define IDTXFLOW_GODOT_API
#endif