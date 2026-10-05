#pragma once

#include <memory>
#include <mutex>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <pxr/usd/usd/stage.h>

#include <idtxflow/async/StageLoadTask.h>
#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include "idtxflow/converter/StageHandle.h"

/**
 * This node represents an USD Stage or an USD Layer defined by an *.usd[a|c|z] file.
 * All converted prims that belong to the usd layer will be assigned as children to this node. However, the attached
 * children are either hidden within the node tree and the whole node is representing an instance of a packed scene,
 * or the whole tree of children is visible.
 *
 * ## Async loading
 *
 * Stage opening (including HTTP fetching via the resolver) is performed on a background
 * thread using `idtxflow::async::StageLoadTask`. During loading, a placeholder cube is
 * displayed at this node's position. The following signals are emitted:
 *
 * - `stage_loading_started` — emitted when async stage loading begins
 * - `stage_loading_finished(success: bool)` — emitted when loading and conversion completes
 */
class UsdStageNode3D : public godot::Node3D, public IUsdNode3D
{
    GDCLASS(UsdStageNode3D, Node3D)
    IUSDNODE(UsdStageNode3D, false)
    
public:
    /*********************** Godot Lifecycle Methods **********************************/
    void _enter_tree() override;
    void _ready() override;
    void _exit_tree() override;

    /**
     * Handle engine notifications
     */
    void _notification(int p_what);

    /**
     * Set the URI of the stage that shall be opened and converted
     * @param path 
     */
    void set_stage_uri(const godot::String& path);

    /**
     * Get the URI of the stage tah was opened and converted
     * @return 
     */
    godot::String get_stage_uri() const { return stage_uri_; }

    /**
     * Set the cached scene name. Used internally after stage conversion to persist the generated cache filename.
     * @param name The cached scene filename
     */
    void set_cached_scene_name(const godot::String& name) { cached_scene_name_ = name; }

    /**
     * Get the cached scene name that was generated after a successful stage conversion.
     * @return The cached scene filename
     */
    godot::String get_cached_scene_name() const { return cached_scene_name_; }

    /**
     * Force a fresh (re)load of the stage at the current `stage_uri_`, bypassing
     * both the URI-equality short-circuit in set_stage_uri() and the cached-scene
     * reuse in _reconstruct_node(): tears down the stage + converted children and
     * re-runs the open/convert path.
     *
     * When `clear_http_cache` is true (default) this URL is evicted first so it is
     * re-downloaded; referenced assets keep their cache. The stale .scn is not deleted,
     * the convert path ignores it and overwrites it only on a successful rebuild.
     *
     * No-op when `stage_uri_` is empty, the node is not in the tree, or a load is
     * already running.
     */
    void reload(bool clear_http_cache = true);

    /**
     * Evict a single URL from the shared HTTP asset cache. Static so the import
     * flow can force-fresh a session root *before* assigning it to `stage_uri`,
     * giving a single fresh load instead of a load-then-reload.
     */
    static void evict_http_cache_entry(const godot::String& url);

    /**
     * Opens the stage at stage_uri_ asynchronously on a background thread,
     * then calls the method passed to it via name with call_deferred to continue execution on the main thread
     */
    void open_stage_and_then(const godot::StringName& next_method_name);
    
    /**
     * Getter to retrieve the usd stage this node has opened.
     * Returns an empty ref when no live stage handle exists: before the first
     * open/convert, after loading a cached scene whose children are kept without
     * reopening the stage, or after the node released its handle on teardown.
     * @return the live stage, or an empty UsdStageRefPtr when none is loaded
     */
    [[nodiscard]]
    pxr::UsdStageRefPtr get_stage() const { return stage_handle_ ? stage_handle_->Stage() : pxr::UsdStageRefPtr(); }

    /**
     * Check if the stage is currently being loaded asynchronously.
     */
    bool is_loading() const { return is_loading_; }

    /// The authoring bridge for this node's live stage, created on first use.
    /// Returns null when there is no live stage. Non-owning pointer; the node
    /// owns the bridge.
    idtxflow::net::ports::IStageBridge* get_or_create_bridge();

    /// Whether this node authors local (session-less) transform edits into its
    /// stage. Off by default; collaboration is unaffected either way.
    void set_local_authoring(bool enabled);
    bool get_local_authoring() const { return local_authoring_; }

    /// Whether moving the stage's placement root (its defaultPrim, e.g. "/World")
    /// is authored into USD. Off by default: the root carries display-only
    /// placement, so its move is not persisted (children are unaffected). On:
    /// the root's transform is authored too.
    void set_author_placement_root(bool enabled) { author_placement_root_ = enabled; }
    bool get_author_placement_root() const { return author_placement_root_; }

    /// Author a converted child's transform into this node's stage (routes
    /// through the edit controller). Entry point for the node transform triggers.
    void author_node_transform(godot::Node3D* child);
    
protected:
    /**
     * reconstructing the node either after loading the scene this node is contained in, or during an
     * _exit_tree -> _enter_tree cycle
     */
    void _reconstruct_node();

    /**
     * Called via call_deferred, once the stage has been loaded in an async worker thread to perform the actual
     * stage conversion into the Godot scene tree. This has to happen on the main thread
     */
    void _convert_stage();

    /**
     * Called via call_deferred, once the stage has been loaded in an async worker thread to load the already
     * converted stage from it's cached scene representation. This has to happen on the main thread
     */
    void _load_converted_stage();
    
    /**
     * Finalize the conversion of the usdPrims->godotNodes while recursively setting the owner of each node
     * as well as the reference to their outermost StageNode3D reference
     * @param node Node to configure (and all the children)
     * @param owner Owner to be set for this node
     */
    void _configure_nodes_recursive(godot::Node3D* node, godot::Node* owner, bool register_compute = false);

    /**
     * remove all child nodes that has been converted as part of the referenced usd stage
     */
    void _cleanup_nodes();

    /**
     * Generate a unique file name for the cached scene (*.tscn/*.scn) file based on the stage URI and
     * whether it has been opened with an overlay layer.
     * @param stage_uri The original stage URI
     * @param binary whether to use tscn or scn format
     * @return 
     */
    godot::String _generate_cached_scene_name(const godot::String& stage_uri, bool binary = true);

    /**
     * Pack the current converted stage-scene and save the same. The call to this function will be deferred
     * to ensure all children are added to the scene tree and ownership is stored
     */
    void _pack_and_save_cached_scene();
    
    static void _bind_methods();
    
    bool node_ready_ = false;
    godot::String stage_uri_;
    godot::String cached_scene_name_;
    std::unique_ptr<idtxflow::converter::StageHandle> stage_handle_;

    // Authoring bridge over the live stage; owned here, lifetime == the stage.
    std::unique_ptr<idtxflow::net::ports::IStageBridge> bridge_;

    // Opt-in: author local (session-less) transform edits into the stage.
    bool local_authoring_ = false;

    // Opt-in: also author the placement root's transform (default: skip it).
    bool author_placement_root_ = false;

    // --- Async loading state ---
    
    // The async stage load task (single-use per load operation)
    std::unique_ptr<idtxflow::async::StageLoadTask> pending_load_task_;
    
    // The result from the worker thread, protected by result_mutex_
    idtxflow::async::StageLoadResult pending_result_;
    std::mutex result_mutex_;

    // Whether an async load is currently in progress
    bool is_loading_;
};
