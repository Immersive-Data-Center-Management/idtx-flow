# Stage Authoring — node-owned bridge

How a Godot transform edit becomes a USD edit on the live stage, for **both**
local (session-less) editing and collaboration, through a single path owned by the
stage node.

This is the authoring half of the client. The collaboration *wire journey* (how an
authored edit reaches peers and how a peer edit is applied) is in
[transform-sync-flow.md](transform-sync-flow.md); the engine-agnostic core and its
ports are in [net-core.md](net-core.md); the Godot adapters in
[godot-binding.md](godot-binding.md).

---

## The one-bridge-per-stage model

Each `UsdStageNode3D` **owns** its authoring bridge:

- `UsdStageNode3D::get_or_create_bridge()` lazily creates one `StageBridge` over
  the node's live stage and returns it on every later call. One bridge → one USD
  `TfNotice` listener → one loopback-suppression flag per stage.
- The bridge's lifetime equals the stage's: it is reset **before**
  `stage_handle_.reset()` at every teardown site, so `~StageBridge` can revoke its
  TfNotice on a still-live stage.
- Collaboration does **not** create its own bridge. `IdtxClient::attach_bridge`
  *borrows* the node's bridge (`get_or_create_bridge()`) and hands it to
  `CollabEngine::attach_stage`; detach only unhooks the engine's sink. One stage
  therefore has exactly one bridge and one TfNotice listener, whether or not a
  session is attached.

The bridge, the edit controller, and the transform codec live in
`source/stage_ops/`, an edit-agnostic home because authoring serves local editing
as well as collaboration.

---

## The unified gate — local OR session, one path

A converted prim node whose transform changes (gizmo drag or Inspector edit) calls
`UsdStageNode3D::author_node_transform(child)`, which routes through a stateless
`StageEditController::author_from_node(child, authoring_enabled, author_root)`:

```
NOTIFICATION_TRANSFORM_CHANGED
        ▼
UsdStageNode3D::author_node_transform(child)
        ▼
StageEditController::author_from_node(child, local_authoring_, author_placement_root_)
        │   skip if  !bridge->has_on_changed()  AND  !authoring_enabled
        │   skip if  node has no prim path
        │   skip if  bridge->is_stage_root(prim_path)  AND  !author_root
        ▼
bridge->author_local_edit( transform_to_prim_edit(prim_path, child->get_transform()) )
```

- **Inclusive gate.** The edit is authored when **either** a session drives the
  stage (a change-report sink is installed → `has_on_changed()`), **or** the node
  opted into local authoring (`local_authoring`, an Inspector bool, off by
  default). Local simply means *no sink is installed, so nothing broadcasts* — the
  write still lands in the stage.
- **Mutual exclusion** is preserved: a session installs the sink, so the local
  branch stands down (it only acts when there is no sink). One move → one author.
- The controller holds the bridge as a non-owning pointer; the node owns it.

---

## What the bridge authors

`StageBridge::author_local_edit` → `author_to_usd(prim_path, Transform3D)`:

- **Edit target.** `EditTarget{SessionLayer, RootLayer, OverrideLayer}` selects the
  composed layer written. Default `SessionLayer` is the in-memory, non-destructive
  override slot (the opened file is untouched); `RootLayer` edits the opened file
  directly; `OverrideLayer` behaves as `SessionLayer`. Local and collab both use
  the session-layer slot.
- **Local transform, not world.** The node's *local* transform is authored. A
  parent move changes a child's world transform but not its local, so children are
  not re-authored on a parent move.
- **Unchanged-matrix guard.** If the stored matrix already equals the new one
  (within `1e-9`), the write is skipped. Godot fires
  `NOTIFICATION_TRANSFORM_CHANGED` on descendants when an ancestor moves, and
  conversion-time writes re-set the imported value; re-authoring the same value is
  a redundant USD write that would still trip a `TfNotice` (and, in a session, a
  broadcast), so it bails.
- **Spine-axis strip.** Cone/Cylinder carry a load-time presentation rotation; the
  author path strips it so USD stores the raw orientation (byte-identical to the
  loader's bake). No-op for every other prim type. See
  [transform-sync-flow.md](transform-sync-flow.md).

---

## Placement root — display-only, not authored (transform policy)

The stage's **placement root** is its `defaultPrim` (e.g. `/World`). It carries
display-only placement (metres-per-unit scale + up-axis rotation), so moving it is
a Godot-only gesture and is **not** authored by default:
`StageEditController` skips a prim for which `bridge->is_stage_root(prim_path)` is
true, unless the node sets `author_placement_root` (an Inspector bool, default
off). `is_stage_root` resolves from `defaultPrim`, so it survives the `.scn` cache
reload.

This skip is a **transform-authoring policy**, scoped to transform edits - it is
not a universal "the root is untouchable" rule.

---

## Nested stages & sub-stage routing

A referenced/payloaded sub-scene loads as its **own** `UsdStageNode3D`, rooted at
`/`, with its **own** bridge, index, and `local_authoring` flag. Consequences:

- A prim inside a sub-scene is authored against the **nearest** ancestor stage
  node, so its edit lands in that sub-stage's layer (not the base stage's). Each
  sub-stage node gates independently (its own `local_authoring` / session sink).
- A move of a *holder* node (a prim that carries a reference/payload arc, e.g.
  `/World/Environment`) is authored on the **parent** stage: it repositions the
  arc's placement in the *referencing* layer. It does **not** modify the referenced
  sub-USD's own root. (A node that *is itself* a sub-stage authors against its
  containing stage via an ancestor walk.)

### Reference/payload holders are typeless — author and read them anyway

Arc holders are idiomatically authored as a **typeless** `def "Name"
(references/payload = …)` whose local transform lives on that prim. A typeless prim
is not an xformable *by schema type*, yet carries a valid `xformOpOrder`. Both the
write and read paths accept the same prims so a holder's authored placement can
also be read back; the outbound path reports a change by *reading* the prim. The
`UsdGeomXformable` API
(`AddTransformOp`/`GetOrderedXformOps`/`GetLocalTransformation`) works on a typeless
prim regardless of the typed-schema check.

---

## Reporting a change back (and de-duping it)

Authoring trips a USD `TfNotice`; `StageBridge::_on_objects_changed` reports each
tracked, changed prim to the engine's sink (`on_changed_`), which gates and
broadcasts (collab) — see [transform-sync-flow.md](transform-sync-flow.md). Two
details matter here:

- **Loopback suppression.** `apply_remote_edit` (and any programmatic author) sets
  `suppress_broadcast_` so the resulting notice is not echoed back out.
- **Within-notice de-dupe.** One authored `Set` surfaces a prim via both its
  property path (`…​.xformOp:transform`) and its prim path in a single notice;
  `_on_objects_changed` coalesces by prim key per dispatch so each prim is reported
  once. (USD may follow with a second info-only notice in a separate dispatch; the
  outbound socket coalesces per prim per frame, so the wire still carries one edit
  per gesture.)

---

## Where things live

| Piece | Location |
|---|---|
| `StageBridge` (`IStageBridge`), `StageEditController`, `TransformCodec` | `source/stage_ops/` |
| `UsdStageNode3D` (owns the bridge, `local_authoring` / `author_placement_root`) | `source/nodes/`, `shared/include/idtxflow_godot/nodes/` |
| Authoring **port** (`IStageBridge`) | `shared/include/idtxflow/net/ports/` |

> Namespace note: these types currently sit in `namespace idtxflow::collab` while
> living under `source/stage_ops/`. Realigning the namespace to the folder
> (`idtxflow::stage_ops` / `::authoring`) is a deferred cohesion pass.

