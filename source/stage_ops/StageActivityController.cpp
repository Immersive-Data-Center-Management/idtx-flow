#include "StageActivityController.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <godot_cpp/classes/collision_object3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/object.hpp>
#include <pxr/usd/sdf/path.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>

#include "PayloadPlacement.h"
#include "StageBridge.h"

using namespace godot;
using namespace pxr;

namespace idtxflow::collab
{
namespace
{
struct Lifetime
{
    uint64_t owner_id = 0;
    uint64_t generation = 1;
    bool alive = true;
    bool in_tree = false;
};

UsdStageNode3D* owner_of(const std::shared_ptr<Lifetime>& lifetime, uint64_t generation = 0)
{
    if (!lifetime->alive || (generation && lifetime->generation != generation)) return nullptr;
    auto* owner = Object::cast_to<UsdStageNode3D>(ObjectDB::get_instance(lifetime->owner_id));
    return owner && !owner->is_queued_for_deletion() ? owner : nullptr;
}

Node* node_of(const std::shared_ptr<Lifetime>& lifetime, uint64_t identity)
{
    if (!owner_of(lifetime)) return nullptr;
    auto* node = Object::cast_to<Node>(ObjectDB::get_instance(identity));
    return node && !node->is_queued_for_deletion() ? node : nullptr;
}

bool valid_path(const String& text)
{
    const std::string value(text.utf8().get_data());
    if (!SdfPath::IsValidPathString(value)) return false;
    const SdfPath path(value);
    return path.IsAbsolutePath() && path.IsPrimPath() && !path.IsAbsoluteRootPath();
}

Dictionary result(const char* status, uint64_t stage = 0, const String& path = {}, const String& reason = {})
{
    Dictionary value;
    value["status"] = status;
    value["blocker_stage_id"] = stage;
    value["blocker_path"] = path;
    value["reason"] = reason;
    return value;
}

UsdStageNode3D* containing_stage(const Node& node)
{
    for (Node* parent = node.get_parent(); parent; parent = parent->get_parent())
        if (auto* stage = Object::cast_to<UsdStageNode3D>(parent)) return stage;
    return nullptr;
}

struct Target
{
    UsdStageNode3D* stage;
    String path;
};

/// The payload holders containing a stage node, innermost first. It depends only on
/// where the stage node sits, not on the prim asked about, so a pass over many prims
/// resolves it once.
struct Containers
{
    std::vector<Target> holders;
    bool available = true;
};

Containers containers_of(UsdStageNode3D* owner, bool require_tree)
{
    Containers out;
    for (UsdStageNode3D* nested = owner; UsdStageNode3D* outer = containing_stage(*nested); nested = outer)
    {
        // An ordinary nested scene has no implicit mapping into a containing USD namespace.
        const String holder = nested->get_prim_path();
        if (holder.is_empty()) break;
        if (!valid_path(holder) || nested->get_stage_node() != outer || outer->find_prim_node(holder) != nested ||
            !outer->get_stage() || (require_tree && !outer->is_inside_tree()))
        {
            out.available = false;
            break;
        }
        out.holders.push_back({outer, holder});
    }
    return out;
}

Dictionary activity(const std::shared_ptr<Lifetime>& lifetime, const String& requested, bool require_tree = true,
                    const Containers* resolved = nullptr)
{
    UsdStageNode3D* owner = owner_of(lifetime);
    if (!owner || !owner->get_stage() || (require_tree && (!lifetime->in_tree || !owner->is_inside_tree())))
        return result("unavailable", 0, {}, "The owning stage is unavailable.");
    if (!valid_path(requested)) return result("unavailable", 0, {}, "An absolute USD prim path is required.");

    const Containers containers = resolved ? *resolved : containers_of(owner, require_tree);
    if (!containers.available)
        return result("unavailable", 0, {}, "The containing payload representation is unavailable.");
    std::vector<Target> targets{{owner, requested}};
    targets.insert(targets.end(), containers.holders.begin(), containers.holders.end());

    // Prefixes remain queryable when an inactive ancestor removes its descendants from composition.
    for (auto item = targets.rbegin(); item != targets.rend(); ++item)
    {
        const auto stage = item->stage->get_stage();
        const SdfPath target(item->path.utf8().get_data());
        for (const SdfPath& prefix: target.GetPrefixes())
        {
            const UsdPrim prim = stage->GetPrimAtPath(prefix);
            if (prim && !prim.IsActive())
            {
                const bool own_target = item->stage == owner && prefix == target;
                return result(own_target ? "inactive" : "ancestor_blocked", item->stage->get_instance_id(),
                              String(prefix.GetText()),
                              own_target ? "This USD prim is inactive." : "An inactive USD ancestor blocks this prim.");
            }
        }
        if (!stage->GetPrimAtPath(target))
            return result("unavailable", 0, {}, "The USD prim is unavailable in its owning stage.");
    }
    return result("active");
}

struct RuntimeSettings
{
    Node::ProcessMode process_mode = Node::PROCESS_MODE_INHERIT;
    bool visible = true;
    CollisionObject3D::DisableMode disable_mode = CollisionObject3D::DISABLE_MODE_REMOVE;
    bool ray_pickable = true;
};

struct Retained
{
    String path;
    Transform3D confirmed;
    bool has_confirmed = false;
    Transform3D authoritative_display;
    bool has_authoritative_display = false;
    bool preserve_display_placement = false;
    bool suspended = false;
    RuntimeSettings original;
};

struct ContentNode
{
    uint64_t identity;
    String path;
    bool is_prim;
    bool nested_stage;
};

struct StringHash
{
    size_t operator()(const String& value) const
    {
        return value.hash();
    }
};

/// For every prim path below @p owner, the node find_prim_node would return for it:
/// the same traversal (last child first, nested stage nodes not entered), keeping the
/// first match. Stored by ObjectID, so a node freed meanwhile is never dereferenced.
using PrimNodeIndex = std::unordered_map<String, uint64_t, StringHash>;

PrimNodeIndex prim_node_index(const UsdStageNode3D& owner)
{
    PrimNodeIndex index;
    std::vector<Node*> stack;
    for (int i = 0; i < owner.get_child_count(); ++i)
        stack.push_back(owner.get_child(i));
    while (!stack.empty())
    {
        Node* node = stack.back();
        stack.pop_back();
        if (IUsdNode3D* usd = IUsdNode3D::from_node(node))
        {
            const String path = usd->get_prim_path();
            if (!path.is_empty() && Object::cast_to<Node3D>(node)) index.emplace(path, node->get_instance_id());
        }
        if (Object::cast_to<UsdStageNode3D>(node) != nullptr) continue;
        for (int i = 0; i < node->get_child_count(); ++i)
            stack.push_back(node->get_child(i));
    }
    return index;
}

String inherited_path(Node* parent, UsdStageNode3D& owner)
{
    for (Node* ancestor = parent; ancestor && ancestor != &owner; ancestor = ancestor->get_parent())
        if (auto* usd = IUsdNode3D::from_node(ancestor); usd && usd->get_stage_node() == &owner)
            return usd->get_prim_path();
    return {};
}

// The content nodes of @p owner: all of them, or with @p root the subtree at @p root
// (below @p parent). @p descend = false yields @p root alone.
std::vector<ContentNode> collect(UsdStageNode3D& owner, Node* root = nullptr, Node* parent = nullptr,
                                 bool descend = true)
{
    struct Pending
    {
        Node* node;
        String inherited;
    };
    std::vector<Pending> pending;
    if (root)
        pending.push_back({root, inherited_path(parent, owner)});
    else
        for (int index = owner.get_child_count() - 1; index >= 0; --index)
            pending.push_back({owner.get_child(index), {}});
    std::vector<ContentNode> nodes;
    while (!pending.empty())
    {
        Pending next = pending.back();
        pending.pop_back();
        Node* node = next.node;
        auto* usd = IUsdNode3D::from_node(node);
        const bool is_prim = usd && usd->get_stage_node() == &owner && valid_path(usd->get_prim_path());
        if (is_prim) next.inherited = usd->get_prim_path();
        const bool nested = Object::cast_to<UsdStageNode3D>(node) != nullptr;
        if (!next.inherited.is_empty()) nodes.push_back({node->get_instance_id(), next.inherited, is_prim, nested});
        // The containing controller owns a carrier, while its controller owns the nested content.
        if (nested || !descend) continue;
        for (int index = node->get_child_count() - 1; index >= 0; --index)
            pending.push_back({node->get_child(index), next.inherited});
    }
    return nodes;
}
} // namespace

struct StageActivityController::State
{
    explicit State(UsdStageNode3D& owner)
        : lifetime(std::make_shared<Lifetime>())
    {
        lifetime->owner_id = owner.get_instance_id();
        lifetime->in_tree = owner.is_inside_tree();
        connect_tree();
    }
    ~State()
    {
        lifetime->alive = false;
    }

    std::shared_ptr<Lifetime> lifetime;
    std::unordered_map<uint64_t, Retained> retained;
    std::unordered_set<uint64_t> corrections;
    bool queued = false;

    // Lookups for one pass over many nodes (refresh, reconcile, flush), so a node costs
    // no search of the whole stage. Valid while the pass runs: a pass changes
    // presentation, never which node represents a prim or where the stage node sits.
    const PrimNodeIndex* pass_index = nullptr;
    const Containers* pass_containers = nullptr;

    struct Pass
    {
        State& state;
        bool owns = false;
        PrimNodeIndex index;
        Containers containers;
        explicit Pass(State& s)
            : state(s)
        {
            auto* owner = owner_of(state.lifetime);
            if (!owner || state.pass_index) return; // an enclosing pass provides them
            index = prim_node_index(*owner);
            containers = containers_of(owner, true);
            state.pass_index = &index;
            state.pass_containers = &containers;
            owns = true;
        }
        ~Pass()
        {
            if (!owns) return;
            state.pass_index = nullptr;
            state.pass_containers = nullptr;
        }
        Pass(const Pass&) = delete;
        Pass& operator=(const Pass&) = delete;
    };

    // Whether @p node is what find_prim_node(path) returns; from the pass table when
    // a pass runs.
    bool is_current_node(UsdStageNode3D& owner, const String& path, Node* node) const
    {
        if (!pass_index) return owner.find_prim_node(path) == node;
        const auto found = pass_index->find(path);
        return found != pass_index->end() && found->second == node->get_instance_id();
    }

    // The node find_prim_node(path) returns, from the pass table; without a pass, the
    // node itself (a single node refreshed as it enters the tree).
    Node* display_node(const ContentNode& content, Node* node) const
    {
        if (!pass_index) return node;
        const auto found = pass_index->find(content.path);
        return found != pass_index->end() ? node_of(lifetime, found->second) : nullptr;
    }

    Dictionary activity_of(const String& path) const
    {
        return activity(lifetime, path, true, pass_containers);
    }

    void connect_tree()
    {
        auto* owner = owner_of(lifetime);
        if (!owner || !owner->is_inside_tree()) return;
        SceneTree* tree = owner->get_tree();
        const Callable callback(owner, "_on_prim_activity_node_added");
        if (!tree->is_connected("node_added", callback)) tree->connect("node_added", callback);
    }

    void disconnect_tree()
    {
        auto* owner = owner_of(lifetime);
        if (!owner || !owner->is_inside_tree()) return;
        SceneTree* tree = owner->get_tree();
        const Callable callback(owner, "_on_prim_activity_node_added");
        if (tree->is_connected("node_added", callback)) tree->disconnect("node_added", callback);
    }

    void schedule()
    {
        auto* owner = owner_of(lifetime);
        if (queued || !owner || !lifetime->in_tree || !owner->is_inside_tree()) return;
        queued = true;
        owner->call_deferred("_flush_prim_activity", lifetime->generation);
    }

    bool owns_current(const ContentNode& content)
    {
        auto* owner = owner_of(lifetime);
        Node* node = node_of(lifetime, content.identity);
        if (!owner || !node || containing_stage(*node) != owner) return false;
        if (!content.is_prim) return inherited_path(node->get_parent(), *owner) == content.path;
        auto* usd = IUsdNode3D::from_node(node);
        return usd && usd->get_stage_node() == owner && usd->get_prim_path() == content.path &&
               is_current_node(*owner, content.path, node);
    }

    // @p node is the node representing content.path (see display_node).
    bool read_display(const ContentNode& content, Node* node, Transform3D& display)
    {
        auto* owner = owner_of(lifetime);
        auto* node3d = Object::cast_to<Node3D>(node);
        if (!owner || !content.is_prim || !node3d) return false;
        auto* bridge = static_cast<StageBridge*>(owner->get_or_create_bridge());
        return bridge && bridge->read_display_transform(std::string(content.path.utf8().get_data()), node3d, display);
    }

    void remember_authoritative(Retained& saved, const Transform3D& display)
    {
        // An accepted display-only placement survives activity toggles until its USD value changes.
        if (!saved.preserve_display_placement || !saved.has_authoritative_display ||
            !saved.authoritative_display.is_equal_approx(display))
        {
            saved.confirmed = display;
            saved.has_confirmed = true;
            saved.preserve_display_placement = false;
        }
        saved.authoritative_display = display;
        saved.has_authoritative_display = true;
    }

    bool suspend(uint64_t identity)
    {
        const auto guard = lifetime;
        const uint64_t expected_generation = guard->generation;
        Node* node = node_of(guard, identity);
        if (!node) return true;
        auto& saved = retained.at(identity);
        if (!saved.suspended)
        {
            saved.original.process_mode = node->get_process_mode();
            if (auto* node3d = Object::cast_to<Node3D>(node)) saved.original.visible = node3d->is_visible();
            if (auto* collision = Object::cast_to<CollisionObject3D>(node))
            {
                saved.original.disable_mode = collision->get_disable_mode();
                saved.original.ray_pickable = collision->is_ray_pickable();
            }
            saved.suspended = true;
        }
        if (auto* collision = Object::cast_to<CollisionObject3D>(node))
        {
            collision->set_disable_mode(CollisionObject3D::DISABLE_MODE_REMOVE);
            if (!owner_of(guard, expected_generation)) return false;
            collision = Object::cast_to<CollisionObject3D>(node_of(guard, identity));
            if (collision) collision->set_ray_pickable(false);
            if (!owner_of(guard, expected_generation)) return false;
        }
        if (auto* node3d = Object::cast_to<Node3D>(node_of(guard, identity))) node3d->set_visible(false);
        if (!owner_of(guard, expected_generation)) return false;
        if (node = node_of(guard, identity)) node->set_process_mode(Node::PROCESS_MODE_DISABLED);
        return owner_of(guard, expected_generation) != nullptr;
    }

    bool restore(uint64_t identity)
    {
        const auto guard = lifetime;
        const uint64_t expected_generation = guard->generation;
        const auto found = retained.find(identity);
        if (found == retained.end() || !found->second.suspended) return true;
        const RuntimeSettings original = found->second.original;
        found->second.suspended = false;
        if (Node* node = node_of(guard, identity)) node->set_process_mode(original.process_mode);
        if (!owner_of(guard, expected_generation)) return false;
        if (auto* collision = Object::cast_to<CollisionObject3D>(node_of(guard, identity)))
        {
            collision->set_disable_mode(original.disable_mode);
            if (!owner_of(guard, expected_generation)) return false;
            collision = Object::cast_to<CollisionObject3D>(node_of(guard, identity));
            if (collision) collision->set_ray_pickable(original.ray_pickable);
            if (!owner_of(guard, expected_generation)) return false;
        }
        if (auto* node3d = Object::cast_to<Node3D>(node_of(guard, identity))) node3d->set_visible(original.visible);
        return owner_of(guard, expected_generation) != nullptr;
    }

    bool refresh(const std::vector<ContentNode>& nodes, bool preparation)
    {
        const auto guard = lifetime;
        const uint64_t expected_generation = guard->generation;
        std::optional<Pass> pass;
        if (nodes.size() > 1) pass.emplace(*this);
        for (const auto& content: nodes)
        {
            Node* node = node_of(guard, content.identity);
            if (!node || (!preparation && !owns_current(content))) continue;
            auto& saved = retained[content.identity];
            if (saved.path != content.path)
            {
                saved.path = content.path;
                saved.has_confirmed = false;
                saved.has_authoritative_display = false;
                saved.preserve_display_placement = false;
            }
            Transform3D display;
            if (preparation && !saved.has_confirmed)
            {
                if (auto* node3d = Object::cast_to<Node3D>(node))
                {
                    saved.confirmed = node3d->get_transform();
                    saved.has_confirmed = true;
                }
            }
            else if (read_display(content, display_node(content, node), display))
            {
                remember_authoritative(saved, display);
            }
            const bool active = String(activity_of(content.path)["status"]) == "active";
            if (!active)
            {
                corrections.insert(content.identity);
                if (!suspend(content.identity)) return false;
            }
            else if (saved.suspended)
                corrections.insert(content.identity);
            if (content.nested_stage && !preparation)
                if (auto* nested = Object::cast_to<UsdStageNode3D>(node_of(guard, content.identity)))
                {
                    nested->get_activity_controller().reconcile();
                    if (!owner_of(guard, expected_generation)) return false;
                }
        }
        return true;
    }

    bool apply_display(const ContentNode& content)
    {
        const auto guard = lifetime;
        const uint64_t expected_generation = guard->generation;
        auto* node = Object::cast_to<Node3D>(node_of(guard, content.identity));
        if (!node || !content.is_prim || !owns_current(content)) return true;
        auto found = retained.find(content.identity);
        if (found == retained.end()) return true;
        Transform3D display;
        if (read_display(content, node, display))
        {
            remember_authoritative(found->second, display);
        }
        if (!found->second.has_confirmed) return true;
        display = found->second.confirmed;
        if (auto* carrier = Object::cast_to<UsdStageNode3D>(node);
            carrier && payload::is_builtin_payload_carrier(*carrier))
            payload::apply_payload_presentation(*carrier, payload::payload_from_display(*carrier, display));
        else
            node->set_transform(display);
        return owner_of(guard, expected_generation) != nullptr;
    }

    void reconcile()
    {
        const auto guard = lifetime;
        const uint64_t expected_generation = guard->generation;
        auto* owner = owner_of(guard, expected_generation);
        if (!owner || !guard->in_tree) return;
        Pass pass(*this);
        const auto nodes = collect(*owner);
        if (!refresh(nodes, false)) return;
        std::unordered_set<uint64_t> present;
        for (const auto& content: nodes)
            present.insert(content.identity);
        for (auto it = retained.begin(); it != retained.end();)
        {
            if (!present.count(it->first))
            {
                corrections.erase(it->first);
                it = retained.erase(it);
            }
            else
                ++it;
        }
        schedule();
    }

    void flush(uint64_t generation)
    {
        const auto guard = lifetime;
        const uint64_t expected_generation = guard->generation;
        auto* owner = owner_of(guard, expected_generation);
        if (!owner || generation != guard->generation || !guard->in_tree || !owner->is_inside_tree()) return;
        queued = false;
        Pass pass(*this);
        const auto nodes = collect(*owner);
        if (!refresh(nodes, false)) return;
        {
            payload::PresentationScope scope;
            std::unordered_set<uint64_t> notifications;
            for (const auto& content: nodes)
            {
                if (!corrections.count(content.identity) || !owns_current(content)) continue;
                std::vector<Node*> pending;
                if (Node* node = node_of(guard, content.identity)) pending.push_back(node);
                while (!pending.empty())
                {
                    Node* node = pending.back();
                    pending.pop_back();
                    if (!notifications.insert(node->get_instance_id()).second) continue;
                    for (int index = 0; index < node->get_child_count(); ++index)
                        pending.push_back(node->get_child(index));
                }
                if (!apply_display(content)) return;
            }
            // Corrected ancestors change descendant world transforms, including nested payload content.
            // Consume only those notifications at this safe deferred boundary, preserving unrelated local edits.
            for (uint64_t identity: notifications)
            {
                if (auto* node = Object::cast_to<Node3D>(node_of(guard, identity)); node && node->is_inside_tree())
                    node->force_update_transform();
                if (!owner_of(guard, expected_generation)) return;
            }
            for (const auto& content: nodes)
                if (owns_current(content) && String(activity_of(content.path)["status"]) == "active" &&
                    !restore(content.identity))
                    return;
        }
        corrections.clear();
        owner = owner_of(guard, expected_generation);
        // Consumers re-query after all display and runtime settings are coherent; they may destroy this controller.
        if (owner) owner->emit_signal("prim_activity_changed");
    }
};

StageActivityController::StageActivityController(UsdStageNode3D& owner)
    : state_(std::make_unique<State>(owner))
{
}
StageActivityController::~StageActivityController() = default;
Dictionary StageActivityController::query(const String& prim_path, bool require_tree) const
{
    return activity(state_->lifetime, prim_path, require_tree);
}
bool StageActivityController::allows_local(const String& prim_path) const
{
    return String(query(prim_path)["status"]) == "active";
}

void StageActivityController::reject_node(Node3D* node)
{
    const auto guard = state_->lifetime;
    auto* owner = owner_of(guard);
    auto* usd = IUsdNode3D::from_node(node);
    if (!owner || !usd || usd->get_stage_node() != owner || owner->find_prim_node(usd->get_prim_path()) != node) return;
    const uint64_t identity = node->get_instance_id();
    auto& saved = state_->retained[identity];
    saved.path = usd->get_prim_path();
    Transform3D display;
    if (state_->read_display({identity, saved.path, true, false}, node, display))
    {
        state_->remember_authoritative(saved, display);
    }
    state_->corrections.insert(identity);
    state_->schedule();
}

void StageActivityController::confirm_node(Node3D* node, bool authoritative)
{
    auto* owner = owner_of(state_->lifetime);
    auto* usd = IUsdNode3D::from_node(node);
    if (!owner || !usd || usd->get_stage_node() != owner || owner->find_prim_node(usd->get_prim_path()) != node ||
        (!authoritative && !allows_local(usd->get_prim_path())))
        return;
    const uint64_t identity = node->get_instance_id();
    auto& saved = state_->retained[identity];
    saved.path = usd->get_prim_path();
    Transform3D display;
    if (authoritative) saved.preserve_display_placement = false;
    if (state_->read_display({identity, saved.path, true, false}, node, display))
        state_->remember_authoritative(saved, display);
    if (!authoritative)
    {
        saved.confirmed = node->get_transform();
        saved.has_confirmed = true;
        saved.preserve_display_placement = true;
    }
}

void StageActivityController::reconcile()
{
    state_->reconcile();
}
void StageActivityController::prepare_subtree(Node* root, Node* parent)
{
    auto* owner = owner_of(state_->lifetime);
    if (owner && root) state_->refresh(collect(*owner, root, parent), true);
}
void StageActivityController::node_added(Node* node)
{
    const auto guard = state_->lifetime;
    auto* owner = owner_of(guard);
    if (!node || !owner || !guard->in_tree || node == owner || containing_stage(*node) != owner) return;
    // The tree reports every node of an added subtree, parents first, so refreshing
    // just this node covers the subtree; collecting it here as well would refresh a
    // node once per ancestor.
    if (state_->refresh(collect(*owner, node, node->get_parent(), /*descend=*/false), true)) state_->schedule();
}
void StageActivityController::enter_tree()
{
    state_->lifetime->in_tree = true;
    state_->connect_tree();
    state_->reconcile();
}
void StageActivityController::exit_tree()
{
    state_->disconnect_tree();
    ++state_->lifetime->generation;
    state_->lifetime->in_tree = false;
    state_->queued = false;
    if (auto* owner = owner_of(state_->lifetime)) owner->emit_signal("prim_activity_changed");
}
void StageActivityController::reset()
{
    ++state_->lifetime->generation;
    state_->retained.clear();
    state_->corrections.clear();
    state_->queued = false;
    state_->schedule();
}
void StageActivityController::flush(uint64_t generation)
{
    state_->flush(generation);
}
} // namespace idtxflow::collab
