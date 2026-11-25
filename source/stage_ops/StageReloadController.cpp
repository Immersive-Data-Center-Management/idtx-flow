#include "StageReloadController.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/object.hpp>

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>

#include <idtxflow/resolver/UrlCanonical.h>
#include <idtxflow/utils/Logger.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>

using namespace godot;

namespace idtxflow::collab
{
namespace
{
IDTX_LOG_CATEGORY("StageReloadController")

// Whether @p stage composes the layer at @p canonical (root, sublayer,
// reference or payload). Compared in the spelling the resolver gives layer
// identifiers, so a server-sent URL matches however a reference spelled it.
bool stage_uses(const pxr::UsdStageRefPtr& stage, const std::string& canonical)
{
    if (!stage) return false;
    for (const pxr::SdfLayerHandle& layer: stage->GetUsedLayers())
    {
        if (layer && idtxflow::resolver::CanonicalUrl(layer->GetIdentifier()) == canonical) return true;
    }
    return false;
}

// Evict every remote (http/https) layer @p stage composes from the HTTP cache, so
// the next load of any of them downloads it again. Local and anonymous layers
// (the session layer) are not cached and are skipped.
void evict_remote_layers(const pxr::UsdStageRefPtr& stage)
{
    if (!stage) return;
    for (const pxr::SdfLayerHandle& layer: stage->GetUsedLayers())
    {
        if (!layer) continue;
        const std::string& id = layer->GetIdentifier();
        if (id.rfind("http://", 0) == 0 || id.rfind("https://", 0) == 0)
        {
            UsdStageNode3D::evict_http_cache_entry(String(id.c_str()));
        }
    }
}

// Every UsdStageNode3D in the subtree of @p node, @p node included, parents
// before their children (nested payload stages are children of their holder).
void collect_stage_nodes(Node* node, std::vector<UsdStageNode3D*>& out)
{
    if (node == nullptr) return;
    if (UsdStageNode3D* stage_node = Object::cast_to<UsdStageNode3D>(node)) out.push_back(stage_node);
    for (int i = 0; i < node->get_child_count(); ++i)
    {
        collect_stage_nodes(node->get_child(i), out);
    }
}

bool is_below_any(const Node* node, const std::vector<UsdStageNode3D*>& ancestors)
{
    for (const Node* p = node->get_parent(); p != nullptr; p = p->get_parent())
    {
        if (std::find(ancestors.begin(), ancestors.end(), p) != ancestors.end()) return true;
    }
    return false;
}
} // namespace

StageReloadController::StageReloadController(Callable finished, Callable cancelled, CompleteFn on_complete)
    : finished_(std::move(finished)),
      cancelled_(std::move(cancelled)),
      on_complete_(std::move(on_complete))
{
}

void StageReloadController::reload_file(const std::string& session_id, Node* session_root, const std::string& file_url)
{
    // Every later fetch of the file downloads it again.
    UsdStageNode3D::evict_http_cache_entry(String(file_url.c_str()));

    ReloadState& state = reloads_[session_id];
    const std::string wanted = idtxflow::resolver::CanonicalUrl(file_url);

    // The session's stage nodes that compose the file: its root node, which
    // references or sublayers it, or is it, and nested payload stages, which
    // load it as their own root. A node below another affected one is rebuilt
    // with it.
    std::vector<UsdStageNode3D*> affected;
    std::vector<UsdStageNode3D*> nodes;
    collect_stage_nodes(session_root, nodes);
    for (UsdStageNode3D* node: nodes)
    {
        const uint64_t id = node->get_instance_id();
        if (state.loading.count(id) > 0)
        {
            // Mid-rebuild for an earlier file: it may already have fetched this
            // one before the eviction above, so it is rebuilt once more.
            state.again.insert(id);
            continue;
        }
        if (is_below_any(node, affected)) continue;
        if (stage_uses(node->get_stage(), wanted)) affected.push_back(node);
    }

    // Release every affected stage, and the stages nested below it, before any
    // new load starts. As long as one stage still holds the old in-memory layer,
    // the layer registry hands a new load that copy instead of the re-fetched
    // file. Nested stages go first: removing them from the tree alone frees
    // them only at the end of the frame.
    for (UsdStageNode3D* node: affected)
    {
        std::vector<UsdStageNode3D*> subtree;
        collect_stage_nodes(node, subtree);
        for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
        {
            (*it)->unload_stage();
        }
    }
    for (UsdStageNode3D* node: affected)
    {
        start_node_reload(session_id, node);
    }

    // Nothing of this session uses the file (any more): nothing to wait for.
    complete_if_idle(session_id);
}

void StageReloadController::rebuild_session(const std::string& session_id, Node* session_root)
{
    ReloadState& state = reloads_[session_id];

    UsdStageNode3D* root = Object::cast_to<UsdStageNode3D>(session_root);
    if (root != nullptr)
    {
        std::vector<UsdStageNode3D*> subtree;
        collect_stage_nodes(root, subtree);

        // Any file the session's stages use may have changed while the socket was
        // down; the LayerReloaded for it was lost with the connection.
        for (UsdStageNode3D* node: subtree)
        {
            evict_remote_layers(node->get_stage());
        }

        const uint64_t root_id = root->get_instance_id();
        if (state.loading.count(root_id) > 0 || root->is_loading())
        {
            // A load is in flight (a rebuild, or the stage's first load): it may
            // have fetched files before the eviction above, so it is rebuilt once
            // more when it finishes. start_node_reload only starts tracking it,
            // since reload() does nothing while the node is loading.
            start_node_reload(session_id, root);
            state.again.insert(root_id);
        }
        else
        {
            // Release nested stages first, as for a reload: as long as one stage
            // still holds an old in-memory layer, a new load is handed that copy
            // instead of the re-fetched file. Rebuilding the root replaces every
            // nested stage, so none of them is waited for any longer; unloading
            // one still loading also reports it cancelled.
            for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
            {
                state.loading.erase((*it)->get_instance_id());
                state.again.erase((*it)->get_instance_id());
                (*it)->unload_stage();
            }
            start_node_reload(session_id, root);
        }
    }

    // No stage to rebuild (not loaded or bound yet): just request the state.
    complete_if_idle(session_id);
}

void StageReloadController::start_node_reload(const std::string& session_id, Node* node)
{
    UsdStageNode3D* stage_node = Object::cast_to<UsdStageNode3D>(node);
    if (stage_node == nullptr) return;

    const uint64_t id = stage_node->get_instance_id();
    // Every started load ends with exactly one of the two signals; whichever fires
    // first disconnects the other.
    const Callable done_cb = finished_.bind(String(session_id.c_str()), static_cast<int64_t>(id));
    const Callable cancel_cb = cancelled_.bind(String(session_id.c_str()), static_cast<int64_t>(id));
    if (!stage_node->is_connected("stage_loading_finished", done_cb))
    {
        stage_node->connect("stage_loading_finished", done_cb, Object::CONNECT_ONE_SHOT);
    }
    if (!stage_node->is_connected("stage_loading_cancelled", cancel_cb))
    {
        stage_node->connect("stage_loading_cancelled", cancel_cb, Object::CONNECT_ONE_SHOT);
    }

    // The file was evicted already; the node's own URL keeps its cache unless it is
    // that file.
    stage_node->reload(/*clear_http_cache=*/false);
    if (stage_node->is_loading())
    {
        reloads_[session_id].loading.insert(id);
        return;
    }

    // The node refused to load (not in the tree, no URI): nothing will finish.
    if (stage_node->is_connected("stage_loading_finished", done_cb))
    {
        stage_node->disconnect("stage_loading_finished", done_cb);
    }
    if (stage_node->is_connected("stage_loading_cancelled", cancel_cb))
    {
        stage_node->disconnect("stage_loading_cancelled", cancel_cb);
    }
}

void StageReloadController::on_node_finished(bool success, const String& session_id, int64_t node_id)
{
    if (Node* node = Object::cast_to<Node>(ObjectDB::get_instance(static_cast<uint64_t>(node_id))))
    {
        const Callable cancel_cb = cancelled_.bind(session_id, node_id);
        if (node->is_connected("stage_loading_cancelled", cancel_cb))
            node->disconnect("stage_loading_cancelled", cancel_cb);
    }

    const std::string sid = session_id.utf8().get_data();
    auto it = reloads_.find(sid);
    if (it == reloads_.end()) return;

    const uint64_t id = static_cast<uint64_t>(node_id);
    it->second.loading.erase(id);
    if (!success)
    {
        IDTX_LOG(IDTX_WARN, "layer reload: rebuilding a stage node of session '{}' failed", sid);
    }

    if (it->second.again.erase(id) > 0)
    {
        start_node_reload(sid, Object::cast_to<Node>(ObjectDB::get_instance(id)));
    }

    complete_if_idle(sid);
}

void StageReloadController::on_node_cancelled(const String& session_id, int64_t node_id)
{
    // The node's load was cut off (set_stage_uri, unload_stage, or the node being
    // freed): stop waiting for it. Only forget it here, never reload it: during
    // NOTIFICATION_PREDELETE a reload would start a load on a dying node. The
    // session completes in poll(), not here, so a rebuild still unloading nested
    // stages during its own setup cannot complete early.
    if (Node* node = Object::cast_to<Node>(ObjectDB::get_instance(static_cast<uint64_t>(node_id))))
    {
        const Callable done_cb = finished_.bind(session_id, node_id);
        if (node->is_connected("stage_loading_finished", done_cb)) node->disconnect("stage_loading_finished", done_cb);
    }

    auto it = reloads_.find(session_id.utf8().get_data());
    if (it == reloads_.end()) return;
    const uint64_t id = static_cast<uint64_t>(node_id);
    it->second.loading.erase(id);
    it->second.again.erase(id);
}

void StageReloadController::poll()
{
    if (reloads_.empty()) return;

    std::vector<std::string> done;
    for (const auto& [sid, state]: reloads_)
    {
        if (state.loading.empty()) done.push_back(sid);
    }
    for (const std::string& sid: done)
    {
        reloads_.erase(sid);
        if (on_complete_) on_complete_(sid);
    }
}

bool StageReloadController::forget(const std::string& session_id)
{
    return reloads_.erase(session_id) > 0;
}

void StageReloadController::complete_if_idle(const std::string& session_id)
{
    auto it = reloads_.find(session_id);
    if (it == reloads_.end() || !it->second.loading.empty()) return;
    reloads_.erase(it);
    if (on_complete_) on_complete_(session_id);
}
} // namespace idtxflow::collab
