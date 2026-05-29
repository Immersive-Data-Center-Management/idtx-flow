#pragma once

#include <memory>
#include <mutex>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>

#include <pxr/usd/usd/stage.h>

#include <idtxflow/async/StageLoadTask.h>
#include <idtxflow_godot/nodes/IUsdNode3D.h>

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
    IUSDNODE(UsdStageNode3D)
    
public:
    /*********************** Godot Lifecycle Methods **********************************/
    void _enter_tree() override;
    void _ready() override;
    void _exit_tree() override;


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
     * Opens the stage at stage_uri_ asynchronously on a background thread,
     * then converts all prims into Godot entities on the main thread.
     * A placeholder cube is shown during loading.
     */
    void open_and_convert_stage();
    
    /**
     * Getter to retrieve the usd stage, this node has loaded and converted
     * @return 
     */
    [[nodiscard]]
    pxr::UsdStageRefPtr get_stage() const { return stage_; }

    /**
     * Check if the stage is currently being loaded asynchronously.
     */
    bool is_loading() const { return is_loading_; }
    
protected:
    /**
     * reconstructing the node either after loading the scene this node is contained in, or during an
     * _exit_tree -> _enter_tree cycle
     */
    void _reconstruct_node();
    
    /**
     * Called via call_deferred from the worker thread callback.
     * Performs stage conversion and scene tree operations on the main thread.
     */
    void _on_stage_loaded();
    
    /**
     * Finalize the conversion of the usdPrims->godotNodes while recursively setting the owner of each node
     * as well as the reference to their outermost StageNode3D reference
     * @param node Node to configure (and all the children)
     * @param owner Owner to be set for this node
     */
    void _configure_nodes_recursive(godot::Node3D* node, godot::Node* owner);

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
    godot::String _generate_cached_scene_name(const godot::String& stage_uri, bool binary = false);

    /**
     * Pack the current converted stage-scene and save the same. The call to this function will be deferred
     * to ensure all children are added to the scene tree and ownership is stored
     */
    void _pack_and_save_cached_scene();
    
    static void _bind_methods();
    
    bool node_ready_ = false;
    godot::String stage_uri_;
    godot::String cached_scene_name_;
    pxr::UsdStageRefPtr stage_ = nullptr;

    // --- Async loading state ---
    
    // The async stage load task (single-use per load operation)
    std::unique_ptr<idtxflow::async::StageLoadTask> pending_load_task_;
    
    // The result from the worker thread, protected by result_mutex_
    idtxflow::async::StageLoadResult pending_result_;
    std::mutex result_mutex_;

    // Whether an async load is currently in progress
    bool is_loading_ = false;
};