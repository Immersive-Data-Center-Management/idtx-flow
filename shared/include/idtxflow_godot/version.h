#pragma once
/**
 * @file version.h
 * @brief IDTXFlow Godot extension API version constants.
 *
 * Third-party extensions can compile-time check against these to verify
 * compatibility with the IDTXFlow base plugin they are linking against.
 */

#define IDTXFLOW_GODOT_VERSION_MAJOR 0
#define IDTXFLOW_GODOT_VERSION_MINOR 2
#define IDTXFLOW_GODOT_VERSION_PATCH 0

/**
 * Encode version as a single integer for easy comparison.
 * Usage:
 *   #if IDTXFLOW_GODOT_VERSION >= IDTXFLOW_GODOT_MAKE_VERSION(0, 1, 0)
 */
#define IDTXFLOW_GODOT_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))

#define IDTXFLOW_GODOT_VERSION \
    IDTXFLOW_GODOT_MAKE_VERSION(IDTXFLOW_GODOT_VERSION_MAJOR, \
                                 IDTXFLOW_GODOT_VERSION_MINOR, \
                                 IDTXFLOW_GODOT_VERSION_PATCH)