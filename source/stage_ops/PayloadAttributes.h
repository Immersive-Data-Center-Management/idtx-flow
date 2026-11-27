#pragma once

/**
 * @file PayloadAttributes.h
 * @brief Propagate holder attribute defaults into nested Godot payload stages.
 *
 * Maps the owning holder to its payload target and updates live and serialized
 * overlays. Stage-load hooks apply current defaults before conversion and refresh
 * restored nodes. This Godot/USD adapter does not issue collaboration requests.
 */

#include <set>
#include <string>
#include <pxr/usd/usd/stage.h>
class UsdStageNode3D;

namespace idtxflow::collab::payload
{
/// Capture the exact holder/source mapping while the converter still has its composed prim.
void initialize_attributes(UsdStageNode3D& carrier, const pxr::UsdPrim& holder);
/// Mirror received holder defaults into the saved overlay and a currently loaded nested stage.
/// Resolves owning node identity on each call; no collaboration requests are issued.
void update_attributes(UsdStageNode3D& carrier, const std::set<std::string>& names);
/// Apply the latest holder defaults to the current load result before any nodes are converted.
/// Called only after UsdStageNode3D has accepted its load generation.
void prepare_attributes(UsdStageNode3D& carrier, const pxr::UsdStageRefPtr& loaded);
/// Refresh cached nodes after attachment using the same external bridge path as a live update.
void refresh_loaded_attributes(UsdStageNode3D& carrier);
} // namespace idtxflow::collab::payload
