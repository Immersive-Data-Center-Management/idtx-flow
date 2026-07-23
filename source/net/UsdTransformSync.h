#pragma once

/**
 * @file UsdTransformSync.h
 * @brief Per-session transform synchronization via a USD TfNotice listener (§9.4).
 *
 * This is the plan's literal §9.4 "live-stage change" mechanism:
 *
 *   Local edit  (Godot gizmo/script moves a node):
 *     - The node's NOTIFICATION_TRANSFORM_CHANGED calls author_local(), which
 *       writes the transform onto the corresponding USD prim (session layer).
 *       Authoring the prim trips the TfNotice listener below.
 *
 *   Stage change (single authoritative hook):
 *     - A pxr::TfNotice listener for UsdNotice::ObjectsChanged fires whenever a
 *       prim's transform changes on the stage — for BOTH locally-authored edits
 *       and inbound-applied edits. When the change is local (not currently
 *       applying a remote broadcast) and the stage is a remote session, it
 *       forwards a TransformUpdate over the WebSocket.
 *
 *   Inbound broadcast:
 *     - apply_remote() sets a suppression flag, authors onto the prim (the
 *       resulting TfNotice is suppressed → no re-broadcast) and updates the
 *       Godot node, then clears the flag.
 *
 * One instance per opened remote stage; owned by IdtxClient. TfNotice may fire
 * on USD worker threads, so the outbound send goes through the thread-safe
 * IdtxSessionSocket (coalesced) via IdtxClient.
 */

#include <cstdint>
#include <string>
#include <unordered_map>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/notice.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/notice.h>

class UsdStageNode3D;

namespace idtxflow
{
namespace net
{
    /**
     * Transform sync controller for a single opened stage. Derives TfWeakBase so
     * it can receive USD change notifications. Owned by IdtxClient (not a Godot
     * object).
     */
    class UsdTransformSync : public pxr::TfWeakBase
    {
    public:
        UsdTransformSync(UsdStageNode3D* stage_node, pxr::UsdStageRefPtr stage);
        ~UsdTransformSync();

        UsdTransformSync(const UsdTransformSync&) = delete;
        UsdTransformSync& operator=(const UsdTransformSync&) = delete;

        /// Index the converted IUsdNode3D descendants (prim_path -> Node3D) and
        /// register the TfNotice listener on the stage.
        void build_index();

        void set_remote(bool remote) { remote_ = remote; }
        bool is_remote() const { return remote_; }

        /// Arm outbound broadcasting. Called after the stage has fully loaded (and
        /// one frame later) so the transform writes performed during USD
        /// conversion do NOT produce phantom broadcasts. Until armed, author_local
        /// still authors into USD but nothing is broadcast.
        void arm() { armed_ = true; }
        bool is_armed() const { return armed_; }

        const std::string& stage_uri() const { return stage_uri_; }
        void set_stage_uri(const std::string& uri) { stage_uri_ = uri; }

        /// Called from a USD node's NOTIFICATION_TRANSFORM_CHANGED: author the
        /// node's (local) transform onto its prim in the live stage. Authoring
        /// trips the TfNotice listener, which performs the conditional broadcast.
        void author_local(const std::string& prim_path, const godot::Transform3D& xform);

        /// Apply an inbound remote transform to the given prim path
        /// (loopback-suppressed): author into USD + update the Godot node.
        void apply_remote(const std::string& prim_path, const godot::Transform3D& xform);

    private:
        struct Tracked
        {
            godot::Node3D* node = nullptr;
        };

        void register_listener();
        void revoke_listener();

        // TfNotice callback: fires on any stage object change (possibly on a
        // USD worker thread).
        void _on_objects_changed(const pxr::UsdNotice::ObjectsChanged& notice,
                                 const pxr::UsdStageWeakPtr& sender);

        // Write a Godot transform onto a prim's xformOp (session layer). Returns
        // true if authored. Shared by author_local and apply_remote.
        bool author_to_usd(const std::string& prim_path, const godot::Transform3D& xform);

        // Read a prim's local transform from the stage as a Godot Transform3D.
        bool read_prim_transform(const std::string& prim_path, godot::Transform3D& out) const;

        UsdStageNode3D*      stage_node_ = nullptr;   // non-owning
        pxr::UsdStageRefPtr  stage_;
        std::string          stage_uri_;
        bool                 remote_ = false;
        bool                 armed_ = false;   // outbound broadcasting enabled?

        // Loopback suppression: set while we author programmatically (local
        // authoring or remote apply) so the resulting TfNotice is not broadcast.
        bool                 suppress_broadcast_ = false;

        pxr::TfNotice::Key   notice_key_;
        bool                 listening_ = false;

        std::unordered_map<std::string, Tracked> tracked_;  // prim_path -> node
    };

} // namespace net
} // namespace idtxflow