# Intended Usage & How-To

Where each part of the client is meant to run (editor vs. a running game), how to
drive a session and local authoring.

For *what the pieces are* see [overview.md](overview.md); for the *data flows* see
[flows.md](flows.md) and [transform-sync-flow.md](transform-sync-flow.md); for *how
authoring works internally* see [stage-authoring.md](stage-authoring.md).

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
| Reload / save-export (`reload()`, `save_stage()`) | ✅ | ✅ | bound node methods, callable from code anywhere; the Inspector buttons that call them are editor-only |
| Import wizard (browse / download / create-join) | ✅ | ❌ | `import_manager.gd` / `plugin.gd` are `@tool` / `EditorPlugin` |
| Session scenes, scene-tab lifecycle, indicators | ✅ | ❌ | `editor/session_scene/` is strictly editor-tier |
| Multi-session | ✅ (one per scene tab) | ✅ (ids, usually one active) | the *tab* mechanism is editor-motivated; the engine holds many sessions by id, but a running game typically has one active scene = one session |
| Inspector buttons (reload, Save As..., commit/save prompts) | ✅ | ❌ | editor UI around the stage node; the underlying methods are runtime-callable |

The rule of thumb: **the GDScript UI and editor orchestration (wizard, tabs,
indicators) are editor-only; the `IdtxClient` public API and the USD nodes are the
runtime surface.** Anything the editor UI does — list files, open/join/commit
sessions, bind a stage, author and sync transforms — can be reconstructed at runtime
through the same API.

---

## Editor usage

Driven through the plugin's main screen and the 3-step import wizard
(`import_manager.gd`) — see [import-manager.md](import-manager.md) and
[flows.md](flows.md) for the full UI and flows. In short:

- **Local import** — pick a `res://` USD; no backend, no session.
- **Server download** — log in, browse, import via an authenticated download; no session.
- **Create / join session** — open a live session for a file; the editor loads the
  stage into a scene, binds it, and arms sync. Each session lives in its **own scene
  tab**; the `editor/session_scene/` coordinator tracks tabs, shows live indicators,
  and offers commit/save on close.
- **Reload** — the stage node's Inspector reload re-downloads the stage, bypassing the
  cached copy, for a **local or non-session** stage whose source changed. It is **not
  safe during an active collaboration session**: reload drops the stage and its
  in-memory session layer and re-downloads only the committed root USD, so uncommitted
  in-session edits (local and peers') are not reapplied and the view desyncs until a
  fresh edit or a rejoin. Commit first, or reload only outside a session.
- **Save As... (experimental)** — the Inspector's Save As... button (next to Reload)
  flattens a snapshot of the stage to a new USD file. See
  [Saving / exporting a stage](#saving--exporting-a-stage-experimental) below.

Multi-session in the editor means **one session per scene tab**; closing the tab
leaves that session. The engine itself holds sessions by id, so a runtime host can
run several, though a running game usually has one active scene and one session.

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

> **Gotcha — enabling the checkbox does not author the current pose.** Authoring is
> triggered only by a transform *change*. A node moved **before** `local_authoring`
> is enabled (or before a session arms) stays a Godot-only change; ticking the box
> does nothing retroactively. The stage catches up only when that node is **moved
> again**. If a prim looks out of sync with the stage, nudge it to re-author.

---

## Saving / exporting a stage (experimental)

Call `UsdStageNode3D.save_stage(out_uri)` (editor and runtime), or the **"Save As..."**
button next to **Reload** in the inspector. It writes a **flattened snapshot** of the
live stage to a new USD file: the source's layer stack with your **session-layer edits
merged in**, composition arcs (references/payloads/variants) preserved as external arcs
(not inlined). Non-destructive (new file; source and server copy untouched; not a server
commit), works local and mid-session, encoding chosen by the extension
(`.usda`/`.usdc`/`.usdz`). The target may be `res://`, `user://`, or absolute, but never
inside the download cache (`user://usd_cache/`).

> Overwrite-source is available only programmatically (`StageSaveController::save_overwrite`,
> not on the node); Saving is **synchronous** and may
> briefly stall on large scenes or `.usdz`.


---

## Session binding — prerequisites & how-to

Binding wires a live session to a loaded stage node so transforms sync. `bind_session`
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
5. Sync **arms** once two gates hold: a short post-attach settle **and** the join
   `SnapshotComplete` (see [transform-sync-flow.md](transform-sync-flow.md)). Edits
   made before arming are not broadcast.

Teardown: `unbind_session(session_id)` detaches the bridge and disconnects the node;
`end_session(session_id)` also closes the socket and asks the backend to drop the
session.

On join, the server sends a snapshot of the session's live prim state, applied over
whatever base the stage loaded, so a client normally catches up to the session
without extra steps. The snapshot reflects the *session's* state, not a diff against
your local copy — so if you suspect a **stale cached base** (for example, another
session committed changes to the file since you last downloaded it), reload the stage
**before** joining. Do not reload during an active session (see
[Local authoring](#editor-usage) and the gotchas below).

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
teardown: unbind_session (leave sync) or end_session (also close socket + drop server-side)
```

The rest of the API is available too (`list_files`, `list_sessions`, `get_session`,
`commit_session`, `fetch_thumbnail`, the `check_*_exists` probes, `is_session_synced`),
so a runtime host can reconstruct the editor's capabilities without the editor UI.

---

## Behavior, limitations & gaps

### By design (intended behavior)

- **The placement root is display-only** — moving the `defaultPrim` (e.g. `/World`) is
  a Godot-only gesture: a whole-stage/root move is not authored or synced (and so not
  persisted) unless `author_placement_root` is on.
- **Moving a reference/payload holder repositions the instance, not the sub-USD** — the
  edit lands on the holder in the referencing layer, not inside the referenced file.
- **Session-layer edits are in-memory** — local and session authoring write the stage's
  session layer, which is non-destructive and not written back to disk on its own.
- **Multi-session is editor-tab-oriented** — in the editor it is one session per scene
  tab; at runtime there is no tab metaphor, so you track session ids yourself.

### Caveats / gotchas

- **Enabling `local_authoring` does not author the current pose** — authoring is
  triggered by a transform *change*, so a node moved before the checkbox is enabled
  (or before a session arms) stays a Godot-only change; move it again to sync.
- **Reload is unsafe during an active session** — the Inspector reload rebuilds the
  stage from the committed file and drops uncommitted in-session edits (local and
  peers'), so the view desyncs. Reload only outside a session, or commit first.

### Current limitations

Client-side:

- **One server / one token at a time** — the client holds a single base URL and a
  single bearer token (a process-wide provider), so it is authenticated to one server
  at a time; logging into another server replaces the current auth. There is no
  simultaneous multi-server / multi-token access.
- **Connection handling is happy-path** — the UI surfaces only *synced* / *syncing*
  (join-snapshot catching up); it does not show *disconnected*, *offline*,
  *reconnecting*, or *server unreachable*. The session socket auto-reconnects with a
  1-10s backoff but no overall timeout or attempt cap, so it keeps retrying
  indefinitely. A dropped-then-reachable server recovers sync via a fresh join
  snapshot, but a persistently unreachable server is not surfaced as an error state.
- **A failed stage load during session create leaks the session** — if the stage
  download/convert fails after a session was created, the wizard removes the node but
  does not end the session, so the backend session and its socket stay alive until a
  later import replaces them or an explicit `end_session`.
- **Mid-session reload/rebuild drops uncommitted edits** — a full stage rebuild
  (Inspector reload, and potentially a tab-switch/reconnect rebuild) re-downloads the
  committed root and discards the in-memory session layer, so uncommitted in-session
  edits (local and peers') disappear from the local view until a fresh edit or rejoin.
  The server still holds them.

Server-side / cross-component:

- **A peer's commit of a prim you also moved may not appear until you rejoin** — if two
  editors each moved prim P in their own session and one commits, the other receives a
  broadcast but P does not visually update, because the receiver's own session-layer
  opinion for P composes stronger than the just-committed root value. Clearing your
  session (rejoin) resolves it; a client that never moved P sees the commit normally.
- **A fresh session can load a stale baseline after a peer commit** — the join snapshot
  carries only the session's uncommitted overrides, not the committed root state; the
  committed baseline arrives via the downloaded file. The client force-re-downloads the
  session root on open to avoid stale caches, but conditional revalidation depends on
  server freshness headers that are not yet emitted on the download response.
- **Editing a referenced child file in a separate session does not live-update
  referencing sessions** — a USD that references/payloads other files loads each
  referenced file as a nested stage; a commit to that child by someone editing it
  directly is not propagated to sessions that merely reference it (the server matches
  only same-file sessions, and there is no referenced-sublayer-changed notification).
- **A server commit can rewrite relative payload/reference paths to absolute, breaking
  downloads** — committing a session flattens the composed layer stack back into the
  file. For a file whose payloads/references are authored as **relative** paths, the
  flatten re-anchors them to **absolute** paths on the server's filesystem. The committed
  file is then written with those absolute paths, overwriting the previously-relative
  ones; a client that downloads it cannot resolve the payloads because the paths point at
  a location on the server rather than inside the downloaded file set. Observed with
  nested-payload scenes.

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