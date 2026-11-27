#pragma once

/**
 * @file StageAttributeController.h
 * @brief Refresh supported resources on existing Godot nodes from composed USD values.
 *
 * StageBridge invokes this after attribute authoring. Reuses the converter's
 * built-in presentation rules and detaches resources before modification while
 * retaining node identity. Networking and USD authoring remain outside this helper.
 */

#include <set>
#include <string>
#include <godot_cpp/classes/node3d.hpp>
#include <pxr/usd/usd/prim.h>

namespace idtxflow::collab
{
/// Updates supported converter resources without replacing nodes, changing activity or sending requests.
class StageAttributeController
{
  public:
    /// Refresh only affected supported resources. Meshes/materials are detached before mutation.
    /// Call under the bridge's authoring guard; a final resource assignment can invoke user callbacks.
    static void refresh(const pxr::UsdPrim& prim, godot::Node3D* node, const std::set<std::string>& names);
};
} // namespace idtxflow::collab
