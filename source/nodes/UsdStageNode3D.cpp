#include <idtxflow_godot/nodes/UsdStageNode3D.h>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include <idtxflow/converter/StageConverter.h>

#include <idtxflow_godot/types/GodotTypes.h>
#include <idtxflow_godot/converter/UsdGodotTypeConverter.h>

#include "converter/UsdGodotStageConverter.h"

using namespace godot;
using namespace pxr;

void UsdStageNode3D::_enter_tree()
{
    Node3D::_enter_tree();
    // As _enter_tree might be called after _exit_tree has been invoked due to switching tabs in the 3d view or the like
    // the children has been completely revoked and the stage handle has been released.
    // Thus we need to reconstruct the childs/nodes and re-open the underlying stage.
    _reconstruct_node();
}

void UsdStageNode3D::_ready()
{
    Node3D::_ready();
    node_ready_ = true;
}

void UsdStageNode3D::_exit_tree()
{
    // Cancel any pending async load
    if (pending_load_task_)
    {
        pending_load_task_->Cancel();
        is_loading_ = false;
    }
    
    stage_handle_.reset();
    _cleanup_nodes();
    Node3D::_exit_tree();
    
}

void UsdStageNode3D::set_stage_uri(const String& path)
{
    if (stage_uri_ == path) return;
    stage_uri_ = path;
       
    // as the stage uri has changed we reset any previous state of this node
    cached_scene_name_ = "";
    stage_handle_.reset();
    _cleanup_nodes();
    
    // Cancel any in-flight async load
    if (pending_load_task_)
    {
        pending_load_task_->Cancel();
        pending_load_task_.reset();
    }

    is_loading_ = false;
    
    // if the new uri is empty, there nothing more todo
    if (stage_uri_.is_empty()) return;
    
    // the setter might be called during deserializing this node from a saved godot scene.
    // at this stage we ar not able to immediately open and convert the stage and need to let the 
    // _enter_tree lifecycle hook do this
    if (!is_inside_tree()) return;
    
    _reconstruct_node();
}

void UsdStageNode3D::open_stage_and_then(const godot::StringName& next_method_name)
{
    if (stage_uri_.is_empty()) return;
    
    // Prevent duplicate loads
    if (is_loading_)
    {
        IDTX_LOGF(IDTX_DEBUG, "Open Stage for: '{}' but still loading...", stage_uri_.utf8().get_data());
        return;
    }
    
    is_loading_ = true;
    emit_signal("stage_loading_started");
    
    // Build the async request from the node's current state
    idtxflow::async::StageLoadRequest request;
    request.uri = stage_uri_.utf8().get_data();
    request.load_set = UsdStage::LoadNone;
    
    if (has_meta("USD_OVERRIDE_LAYER"))
    {
        String override_layer_content = get_meta("USD_OVERRIDE_LAYER");
        request.override_layer_content = override_layer_content.utf8().get_data();
        request.override_layer_id = get_meta("USD_OVERRIDE_LAYERID", "").stringify().utf8().get_data();
    }
    
    // Create a new task for this load operation
    pending_load_task_ = std::make_unique<idtxflow::async::StageLoadTask>();
    
    // Launch async stage open on a worker thread.
    // The callback fires on the worker thread - we use call_deferred to marshal the result back to the main thread
    // for scene tree operations.
    pending_load_task_->LoadAsync(std::move(request),
        [&, next_method_name](idtxflow::async::StageLoadResult result)
        {
            // Store the result in a thread-safe manner
            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                pending_result_ = std::move(result);
                is_loading_ = false;
            }
            
            if (!pending_result_.success())
            {
                IDTX_LOGF(IDTX_ERROR, "Unable to open Stage. Error: '{}'", result.error_message.c_str());
                emit_signal("stage_loading_finished", false);
                return;
            }
    
            if (!pending_result_.stage)
            {
                emit_signal("stage_loading_finished", false);
                return;
            }
            
            // Marshal to main thread via call_deferred
            call_deferred(next_method_name);
        });
}

void UsdStageNode3D::_reconstruct_node()
{
    if (is_inside_tree())
    {
        if (!stage_uri_.is_empty())
        {
            // coming here is most likely the case, when the scene has been loaded or after an _exit_tree -> _enter_tree
            // cycle. If the node has a cached scene name stored, it has been saved in the converted state and thus does not
            // trigger conversion again as all nodes has been loaded already. Otherwise trigger conversion.
            if (cached_scene_name_.is_empty() || !FileAccess::file_exists(cached_scene_name_))
            {
                _cleanup_nodes();
                open_stage_and_then("_convert_stage");
            
                return;
            }
        
            // if the cached scene has been provided and the file exists, we load the cached scene, instantiate it
            // and add it to the scene tree
            if (!cached_scene_name_.is_empty())
            {
                _cleanup_nodes();
                open_stage_and_then("_load_converted_stage");
            }
        }
    }
}

void UsdStageNode3D::_convert_stage()
{
    // Retrieve the result (written by the worker thread)
    idtxflow::async::StageLoadResult result;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        result = std::move(pending_result_);
    }
    is_loading_ = false;
        
    // instantiate the stage converter and convert the contents of the stage into Godot Node3D entities
    auto stage_converter = std::make_unique<idtxflow::converter::UsdStageConverter<idtxflow::types::TargetEngineGodot>>(this, nullptr);
    idtxflow::converter::StageConversionResult<idtxflow::types::TargetEngineGodot> conversion_result = stage_converter->Convert(result.stage);
    if (conversion_result.ConvertedEntities.empty())
    {
        emit_signal("stage_loading_finished", true);
        return;
    }
    
    // store the stage handle
    stage_handle_ = std::make_unique<idtxflow::converter::StageHandle>(std::move(conversion_result.Handle));
    
    // once all nodes are converted, we need to add them as child to the current node.
    // And we need to recursively maintain the owner of all nodes, which is
    // only possible, once the nodes are added to the tree (by adding them as children to this node)
    for (Node3D* node : conversion_result.ConvertedEntities)
    {
        _configure_nodes_recursive(node, this); // we set the owner here, but defer this to ensure adding to the tree happens first
        this->add_child(node); // this invokes _ready() on node at earliest convenient from Godot engine point of view, which expects the "config" already run
    }
    
    // name our-self after the root layer of the stage we opened
    set_name(stage_handle_->Stage()->GetRootLayer()->GetDisplayName().c_str());

    // generate and store a unique cached scene name for this converted stage
    cached_scene_name_ = _generate_cached_scene_name(stage_uri_);
    
    call_deferred("_pack_and_save_cached_scene");
    
    emit_signal("stage_loading_finished", true);
}

void UsdStageNode3D::_load_converted_stage()
{
    // Retrieve the result (written by the worker thread)
    idtxflow::async::StageLoadResult result;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        result = std::move(pending_result_);
    }
    is_loading_ = false;
    
    // store the stage handle
    stage_handle_ = std::make_unique<idtxflow::converter::StageHandle>(std::move(result.stage));
    
    Ref<PackedScene> packed_scene = ResourceLoader::get_singleton()->load(cached_scene_name_);
    Node3D* cached_root = cast_to<Node3D>(packed_scene->instantiate());
    // the instantiated node would be the cached "UsdStageNode3D". Thus, just adding this to the tree
    // would create a recursion. The intention anyway was to use this node as a root only for caching. And "copy"
    // it's children after instantiation to this node would re-create the original structure anyway.
    while (cached_root->get_child_count() > 0) {
        Node* child = cached_root->get_child(0);
        cached_root->remove_child(child);
        child->set_owner(nullptr);
        if (Node3D* child3d = cast_to<Node3D>(child)) {
            _configure_nodes_recursive(child3d, this, true);
            add_child(child3d);
        } else {
            child->queue_free();
        }
    }
    
    // release the instantiated packed scene, all children have been copied over to the actual scene tree
    cached_root->queue_free();
    
    // only after we have transferred the cached scene into the current one we can activate the compute bridge
    // as during stage_handle_ instantiation there is no compute property registered yet
    auto bridge = idtxflow::exec::ExecBridgeManager::Instance().GetExecBridgeForStage(stage_handle_->Stage());
    if (bridge && bridge->GetValueKeyCount() > 0)
    {
        idtxflow::exec::ExecBridgeManager::Instance().ActivateBridge(bridge);
    }
    
    emit_signal("stage_loading_finished", true);
}

void UsdStageNode3D::_configure_nodes_recursive(godot::Node3D* node, godot::Node* owner, bool register_compute)
{
    if (!node) return;
    
    // store the owner
    if (owner && node != owner) node->call_deferred("set_owner", owner);

    // store the owning StageNode3D
    IUsdNode3D* usd_node = IUsdNode3D::from_node(node);
    if (usd_node)
        usd_node->set_stage_node(this);
    
    // if this is a UsdStageNode3D itself, skip traversing the childrens, as this node takes care of it
    // on it's own
    if (dynamic_cast<UsdStageNode3D*>(node)) return;
    
    // if "register_compute" is true, the configuration happens during loading of a cached stage scene. Thus, the 
    // stage converter did not run and registered compute attributes into the ExecComputeBridge. Do this here now
    if (register_compute && usd_node && stage_handle_)
    {
        std::shared_ptr<idtxflow::exec::ExecBridge> bridge = idtxflow::exec::ExecBridgeManager::Instance()
                .GetExecBridgeForStage(stage_handle_->Stage());
        if (pxr::UsdPrim usdPrim = stage_handle_->Stage()->GetPrimAtPath(pxr::SdfPath(usd_node->get_prim_path().utf8().get_data())))
        {
            for (const pxr::UsdAttribute& attribute : usdPrim.GetAttributes())
            {
                if (attribute.HasAuthoredConnections())
                    bridge->RegisterAttributeWithConnection(attribute);
            }
            if (bridge->GetValueKeyCount() > 0)
            {
                bridge->RegisterComputeResultHandler(usdPrim.GetPath(),
                    std::shared_ptr<IExecBridgeHandler>(
                        dynamic_cast<IExecBridgeHandler*>(usd_node),
                        [](IExecBridgeHandler*)
                        {
                            /* the empty shared_ptr destructor ensures that the owner of the converted
                             * node instance is responsible for its lifecycle and releasing the
                             * last instance of the shared_ptr will not delete/free the contained object
                             */
                        }
                    ));
            }
        }
    }
    
    // do this for all th children
    const std::string& stage_path = !stage_handle_->Stage() ? std::string() : stage_handle_->Stage()->GetRootLayer()->GetRealPath();
    for (int i = 0; i < node->get_child_count(); i++) {
        Node* child = node->get_child(i);
        IUsdNode3D* usd_node = IUsdNode3D::from_node(child);
        // if the child is not a USD node, do nothing
        if (!usd_node) continue;
        
        // if the child belongs to another stage (properly happens if there are children of a UsdStageNode3D that
        // has been brought into the scene from the referenced stage at the same level as children that have been imported
        // based on the layer referenced to by a payload), we will not configure the owner.
        std::string child_stage_path = std::string(usd_node->get_stage_path().utf8().get_data());
        if (child_stage_path == stage_path)
            _configure_nodes_recursive(Object::cast_to<Node3D>(child), owner, register_compute);
    }
}

void UsdStageNode3D::_cleanup_nodes()
{
    int32_t child_count = get_child_count();
    // loop backward to ensure index consistency during child removal
    for (int i = child_count; i > 0; i--)
    {
        Node* child = get_child(i-1);
        // remove all direct childs that are marked as USD nodes.
        // if other nodes had been added into the child tree they will also be removed
        // as part of this cleanup
        if (child->has_meta("USD_NODE"))
        {
            remove_child(child);
            child->queue_free();   
        }
    }
}

godot::String UsdStageNode3D::_generate_cached_scene_name(const godot::String& stage_uri, bool binary)
{
    if (stage_uri.is_empty()) return "";
    
    const String cache_dir = "user://usd_cache/";
    const String filename = stage_uri.get_file().get_basename();
    const String file_hash = String::num_int64(stage_uri.hash());
    
    String suffix;
    // if the stage/layer was opened with a provided overlay layer we create a unique hash based on the layer contents
    // to ensure the same original stage can be used and cached with different override layers that result in different
    // converted contents for the scene
    if (has_meta("USD_OVERRIDE_LAYER")) {
        const String override_content = get_meta("USD_OVERRIDE_LAYER");
        suffix = String::num_uint64(override_content.hash());
    } else {
        suffix = String();
    }
    const String extension = binary ? ".scn" : ".tscn";
    
    return cache_dir + filename + "_" + file_hash + "_" + suffix + extension;
}

void UsdStageNode3D::_pack_and_save_cached_scene()
{
    // from converted stage create a packed scene and save it as cached scene
    Ref<PackedScene> packed_scene;
    packed_scene.instantiate();
    Error err = packed_scene->pack(this);
    if (err != OK)
    {
        IDTX_LOGF(IDTX_ERROR, "Unable to pack Scene. Error: {}", static_cast<int>(err));
    } else
    {
        // save the packed scene at the location calculated before
        // Ensure cache directory exists
        String cache_dir = cached_scene_name_.get_base_dir();
        if (!DirAccess::dir_exists_absolute(cache_dir)) {
            DirAccess::make_dir_recursive_absolute(cache_dir);
        }
    
        ResourceSaver::get_singleton()->save(packed_scene, cached_scene_name_);
    }
    packed_scene.unref();
}

void UsdStageNode3D::_bind_methods()
{
    IUSDNODE_IMPLEMENT_BINDINGS(UsdStageNode3D)
    
    ClassDB::bind_method(D_METHOD("set_stage_uri", "path"), &UsdStageNode3D::set_stage_uri);
    ClassDB::bind_method(D_METHOD("get_stage_uri"), &UsdStageNode3D::get_stage_uri);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "stage_uri", PROPERTY_HINT_FILE, "*.usd,*.usda,*.usdc,*.usdz"), "set_stage_uri", "get_stage_uri");

    ClassDB::bind_method(D_METHOD("set_cached_scene_name", "name"), &UsdStageNode3D::set_cached_scene_name);
    ClassDB::bind_method(D_METHOD("get_cached_scene_name"), &UsdStageNode3D::get_cached_scene_name);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "cached_scene_name", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_READ_ONLY),
        "set_cached_scene_name", "get_cached_scene_name");
    
    // Registration required to allow deferred calling
    ClassDB::bind_method(D_METHOD("_pack_and_save_cached_scene"), &UsdStageNode3D::_pack_and_save_cached_scene);
    
    // Internal deferred callbacks invoked after async stage loading (not exposed to user scripts)
    ClassDB::bind_method(D_METHOD("_convert_stage"), &UsdStageNode3D::_convert_stage);
    ClassDB::bind_method(D_METHOD("_load_converted_stage"), &UsdStageNode3D::_load_converted_stage);
    
    // Signals for async loading lifecycle
    ADD_SIGNAL(MethodInfo("stage_loading_started"));
    ADD_SIGNAL(MethodInfo("stage_loading_finished", PropertyInfo(Variant::BOOL, "success")));
}
