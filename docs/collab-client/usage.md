# Intended Usage & How-To

Where each part of the client is meant to run (editor vs. a running game), how to
drive a session and local authoring.

For *what the pieces are* see [overview.md](overview.md); for the *data flows* see
[flows.md](flows.md) and [transform-sync-flow.md](transform-sync-flow.md); for *how
authoring works internally* see [stage-authoring.md](stage-authoring.md).
For Create/Delete/Restore requests and asset insertion, see
[structural-edits.md](structural-edits.md).

---

## Editor vs. runtime — what runs where

The client splits into a **core API** (reachable wherever the extension is loaded)
and **editor tooling** (an `EditorPlugin` and `@tool` scripts that only exist in the
Godot editor).

| Capability | Editor | Runtime | Notes |
|---|---|---|---|
| `IdtxClient` singleton (auth, REST, sessions, sync) | ✅ | ✅ | `Engine.get_singleton("IdtxClient")`; registered by the GDExtension |
| `UsdStageNode3D` + converted prim nodes | ✅ | ✅ | load a stage, read/author transforms |
| Local authoring (`local_authoring`) | ✅ | ✅ | a node property; no editor dependency |
| Session binding (`bind_session` / sync) | ✅ | ✅ | you supply the stage node and session id |
| Create / reference / payload / Delete / Restore requests | ✅ | ✅ | `IdtxClient.request_*`; server broadcasts apply the changes |
| Inactive-prim protection and activity query | ✅ | ✅ | native policy; no editor plugin or script setup required |
| Reload / save-export (`reload()`, `save_stage()`) | ✅ | ✅ | bound node methods, callable from code anywhere; the Inspector buttons that call them are editor-only |
| Import wizard (browse / download / create-join) | ✅ | ❌ | `import_manager.gd` / `plugin.gd` are `@tool` / `EditorPlugin` |
| Session scenes, scene-tab lifecycle, indicators | ✅ | ❌ | `editor/session_scene/` is strictly editor-tier |
| Multi-session | ✅ (one per scene tab) | ✅ (ids, usually one active) | the *tab* mechanism is editor-motivated; the engine holds many sessions by id, but a running game typically has one active scene = one session |
| Inspector buttons (reload, Save As..., commit/save prompts) | ✅ | ❌ | editor UI around the stage node; the underlying methods are runtime-callable |

The rule of thumb: **the GDScript UI and editor orchestration (wizard, tabs,
indicators) are editor-only; the `IdtxClient` public API and the USD nodes are the
runtime surface.** Anything the editor UI does — list files, open/join/commit
sessions, bind a stage, create/deactivate/restore prims, author and sync transforms —
can be reconstructed at runtime through the same API.

---

## Editor usage

Driven through the plugin's main screen and the 3-step import wizard
(`import_manager.gd`) — see [import-manager.md](import-manager.md) and
[flows.md](flows.md) for the full UI and flows. In short:

- **Local import** — pick a `res://` USD; no backend, no session.
- **Server download** — log in, browse, import via an authenticated download; no session.
- **Create / join session** — open a live session for a file; the editor loads the
  stage into a scene, binds it, and arms sync. Each session lives in its **own scene
  tab**; the `editor/session_scene/` coordinator tracks tabs, while separate
  plugin-owned helpers show live indicators and offer a server commit on editor save.
- **Reload** — the stage node's Inspector reload reopens the source, bypassing the
  cached download for a remote stage. Use it outside an active session; see the
  [reload caveat](#caveats--gotchas).
- **Save As... (experimental)** — the Inspector's Save As... button (next to Reload)
  flattens a snapshot of the stage to a new USD file. See
  [Saving / exporting a stage](#saving--exporting-a-stage-experimental) below.

Multi-session in the editor means **one session per scene tab**; closing the tab
leaves that session. The engine itself holds sessions by id, so a runtime host can
run several, though a running game usually has one active scene and one session.

For an experimental editor demonstration of the API, open
`addons/IDTXFlow/editor/editor_prim_tools/run_prim_tools.gd` in Godot's Script editor
and choose **File > Run** while a live-session tab is active. Select a converted
USD prim to use **Create child**, **Add asset** (Reference/Payload), **Delete
selected**, or **Restore selected**. See the
[demo README](../../addons/IDTXFlow/editor/editor_prim_tools/README.md).

---

## Local authoring (session-less editing)

A `UsdStageNode3D` can author gizmo/Inspector transform edits straight into its stage
without any session. Enable it per node:

- **`local_authoring`** (Inspector bool, **off by default**) — when on, moving a
  converted child prim authors its transform into the stage's session layer
  (non-destructive; the opened file is untouched). With it off and no session, moves
  are Godot-only and nothing is authored.
- **`author_placement_root`** (Inspector bool, **off by default**) — the stage's
  placement root (its `defaultPrim`, e.g. `/World`) carries display-only placement and
  is not authored unless this is on.

See [stage-authoring.md](stage-authoring.md) for where edits land and how nested
stages / reference-payload holders route.

For edits made before authoring or synchronization is enabled, see
[Caveats / gotchas](#caveats--gotchas).

---

## Saving / exporting a stage (experimental)

Call `UsdStageNode3D.save_stage(out_uri)` (editor and runtime), or the **"Save As..."**
button next to **Reload** in the inspector. It writes a **flattened snapshot** of the
live stage to a new USD file: the source's layer stack with your **session-layer edits
merged in**, composition arcs (references/payloads/variants) preserved as external arcs
(not inlined). Non-destructive (new file; source and server copy untouched; not a server
commit), callable locally or mid-session, encoding chosen by the extension
(`.usda`/`.usdc`/`.usdz`). The target may be `res://`, `user://`, or absolute, but never
inside the download cache (`user://usd_cache/`).
This experimental export is separate from the normal live-session workflow;
it is not needed to send edits or restore an inactive prim.

> Overwrite-source is available only programmatically (`StageSaveController::save_overwrite`,
> not on the node); Saving is **synchronous** and may
> briefly stall on large scenes or `.usdz`.


---

## Session binding — prerequisites & how-to

Binding wires a live session to a loaded stage node so transforms and structural
broadcasts apply to it. `bind_session`
requires the node to be an already-constructed `UsdStageNode3D`; it attaches (or
rebuilds, on reload) the node's authoring bridge and follows the node's
`stage_loading_finished` / `stage_unloading` lifecycle.

Order of operations:

1. Authenticate: `set_base_url(url)`, then `login(username, password, on_done)`.
2. Obtain a session: `open_new_session(usd_file, mode, auto_commit, on_done)` to create,
   or `open_existing_session(session_id, on_done)` to join. The completion carries the
   `session_id` and the stage download URL.
3. Load that stage into a `UsdStageNode3D` (set `stage_uri`, add to the tree) and wait
   for `stage_loading_finished(success=true)`.
4. `bind_session(session_id, stage_node, remote)` — `remote = true` means outbound
   edits broadcast to peers; `false` binds without broadcasting.
5. Outgoing transform sync **arms** once two gates hold: a short post-attach
   settle **and** the join `SnapshotComplete` (see
   [transform-sync-flow.md](transform-sync-flow.md)). Edits made before arming
   are not broadcast.

`is_session_synced(session_id)` reports only the snapshot latch, and can be true
before the loaded stage has settled. Structural sends check remote mode, snapshot
completion and an open socket; they do not use the transform arming gate. A host
should additionally require a successfully loaded, settled stage and a valid
selection before offering actions.

Teardown: `unbind_session(session_id)` detaches the bridge and disconnects the node;
`end_session(session_id)` also closes the socket and leaves the session. It does
not delete the backend session; the server handles sessions with no remaining
clients according to its own lifecycle rules.

On join, the server sends a snapshot of the session's live prim state, applied over
the base the stage loaded. See [Current limitations](#current-limitations) for
source-file freshness and [Caveats / gotchas](#caveats--gotchas) before reloading.

---

## Runtime usage (flow)

At runtime there is no wizard and no scene-tab orchestration — you drive the
`IdtxClient` singleton directly and manage session ids yourself. The flow mirrors
what the editor does, through the same public API:

```
get the IdtxClient singleton (Engine.get_singleton)
set base URL, then login  ->  on success:
    open_existing_session(id)  (or open_new_session to create)  ->  on success:
        create a UsdStageNode3D, point stage_uri at the session's stage URL, add to tree
        when stage_loading_finished(success) fires:
            bind_session(session_id, stage_node, remote = true)
        ... sync arms after settle + SnapshotComplete; edits now broadcast
teardown: unbind_session (detach sync) or end_session (also close socket + leave session)
```

The rest of the API is available too (`list_files`, `list_sessions`, `get_session`,
`commit_session`, `fetch_thumbnail`, the `check_*_exists` probes, `is_session_synced`),
so a runtime host can reconstruct the editor's capabilities without the editor UI.

---

## Create, delete and restore in a live session

Use `IdtxClient.request_create_prim(session_id, parent_path, spec)`,
`request_delete_prim(session_id, prim_path)`, and
`request_restore_prim(session_id, prim_path)` from your own runtime controls.
The [structural edit guide](structural-edits.md) contains the GDScript examples,
supported types, reference/payload paths and server validation rules.

Creation uses the exact selected USD parent path. The server chooses the name
and default shape dimensions; the request can optionally supply a `Transform3D`.
Requests do not immediately create/remove nodes: server broadcasts apply the result.
Each request returns `0` when not submitted, or a positive request ID after
transport submission. Optionally match it with
`ack_received(session_id, request_id, request_kind, ok, error)` to report
acceptance or rejection for the original action. If you track pending actions,
keep them across selection changes and clear them when their session context ends or disconnects.
An accepted Ack does not mean the scene has finished updating.

Delete sets `active=false`; it does not erase the USD data or remove the retained
Godot nodes. Native activity handling hides blocked content, disables processing
(including descendants set to `PROCESS_MODE_ALWAYS`), removes collision objects
from simulation and disables their ray picking. Rejected local transform edits
are corrected without authoring USD or sending a transform update. External USD
updates remain accepted.

Restore requests `active=true`. Once its broadcast arrives, the native controller
uses the current live USD state and restores saved runtime settings. It does not
request another snapshot or reload the original file. UI can query
`stage_node.get_prim_activity(path)` and refresh on `prim_activity_changed`; the
[binding guide](godot-binding.md#activity-status) describes the result. If an
ancestor is inactive, restore that ancestor first.

---

## Behavior, limitations & gaps

### By design (intended behavior)

- **The placement root is display-only by default** — moving the `defaultPrim`
  (e.g. `/World`) is not authored unless `author_placement_root` is on. That option
  permits local authoring but does not enable outgoing root-transform reports.
- **Moving a reference/payload holder repositions the instance, not the sub-USD** — the
  edit lands on the holder in the referencing layer, not inside the referenced file.
- **Session-layer edits are in-memory** — local and session authoring write the stage's
  session layer, which is non-destructive and not written back to disk on its own.
- **Inactive content is retained and protected** — Delete/Restore changes activity
  without removing and reconstructing the converted Godot nodes. These requests
  do not save a file or commit a session.
- **Multi-session is editor-tab-oriented** — in the editor it is one session per scene
  tab; at runtime there is no tab metaphor, so you track session ids yourself.

### Caveats / gotchas

- **Enabling `local_authoring` does not author the current pose** — authoring is
  triggered by a transform *change*, so an unbound node moved before it is enabled
  stays a Godot-only change; move it again to author. A bound stage may author
  before transform sync arms, but those early changes are not queued for later sending.
- **Reload is unsafe during an active session** — a full stage rebuild, such as
  Inspector reload, re-downloads the committed root and discards the in-memory
  session layer. Uncommitted in-session edits (local and peers') disappear from
  the local view until a fresh edit or rejoin; the server still holds them. Reload
  only outside a session. Ordinary tab re-entry retains a live stage handle,
  including when its content is inactive, and does not require reconstruction.

### Current limitations

Client-side:

- **One server / one token at a time** — the client holds a single base URL and a
  single bearer token (a process-wide provider), so it is authenticated to one server
  at a time; logging into another server replaces the current auth. There is no
  simultaneous multi-server / multi-token access.
- **The existing session indicators have limited connection status** — they surface
  only *synced* / *syncing* (join-snapshot catching up); they do not show *disconnected*, *offline*,
  *reconnecting*, or *server unreachable*. The session socket auto-reconnects with a
  1-10s backoff but no overall timeout or attempt cap, so it keeps retrying
  indefinitely. After reconnecting, the client rebuilds the session's stage from
  re-fetched files and requests the session's state, as after a layer reload.
  Runtime hosts can observe the socket error/disconnection signals.
- **A failed stage load during session create leaks the session** — if the stage
  download/convert fails after a session was created, the wizard removes the node but
  does not end the session, so the backend session and its socket stay alive until
  explicit teardown such as `end_session`.
- **Payload child edits have no outer-session path mapping** — references generally
  compose into the outer stage namespace; unloaded payloads use separate nested
  stage nodes. Moving a supported payload holder can sync its placement, but
  editing a nested payload child does not automatically reach the outer session.
- **Payload placement correction requires one nonanimated built-in `Xform` default
  root** — any number of descendants is supported. Direct geometry roots, animated
  roots and multiple converted roots keep their existing conversion behavior;
  correct subsequent placement editing is not guaranteed for them.
- **Explicit asset target selection is not supported** — core accepts suffixes
  such as `./asset.usda</Asset/Part>`, but the client does not split a received
  suffix into an asset identifier and `SdfPath`, or select the corresponding
  payload subtree. Use assets with a defaultPrim.
- **Structural requests require a live session** — Create/Delete/Restore enter
  through `IdtxClient`. `local_authoring` enables transform authoring; it does not
  provide standalone structural editing.
- **Arbitrary editor operations are not synchronized** — native Scene-tree
  additions, removals, reparenting and Inspector property changes are not generally
  converted into USD/server requests. Activity protection covers the retained
  nodes' transforms and runtime participation, not every editor action.

Server-side / cross-component:

- **Shared-source changes do not automatically refresh open stages** — core sends
  `LayerReloaded` to affected live sessions, including sessions using the source
  through composition, but the client does not process those notifications to
  refresh their contents. Existing session opinions also compose stronger than
  changed source values.
- **A snapshot does not replace source-file freshness** — the client must load the
  committed baseline as well as apply live snapshot state. The editor evicts the
  downloaded root cache entry before opening a session, but this is not shared-layer
  refresh for already open owners or nested assets.
- **Commit paths and session placement are separate contracts** — current core
  preserves relative reference/payload paths when committing. Created client arcs
  still need the root anchoring described in [structural-edits.md](structural-edits.md), because live
  authoring normally targets an anonymous session layer.

### Developer notes / technical debt

These do not change observable behavior for an end user, but are worth knowing when
extending the client.

- **`bind_session` validation is minimal.** It rejects a non-`UsdStageNode3D` node, and
  defers the bridge attach until the stage is loaded, but it does not reject an empty
  session id or a node with no `stage_uri`, and the "stage not loaded yet" case returns
  silently.
- **A session is not invalidated when the node's `stage_uri` changes.** The binding
  tracks the node by id but not the URI it was bound for, so repointing a bound
  `UsdStageNode3D` at a different file does not unbind the session; a later load could
  attach the bridge for the wrong document under the old session. Re-bind explicitly
  after changing a bound node's `stage_uri`.
- **Namespace vs folder mismatch.** `StageBridge`, `StageEditController`, and
  `TransformCodec` live under `source/stage_ops/` but sit in `namespace
  idtxflow::collab`, even though authoring now serves local (non-collab) editing too.
  Realigning the namespace to the folder (`idtxflow::stage_ops` / `::authoring`)
---