# Import Manager (editor UI)

The import manager is the editor-only GDScript UI a user drives to bring a USD file
into the scene. It lives under `addons/IDTXFlow/import_manager/` (plus the plugin
entry in `addons/IDTXFlow/`). It contains no networking itself: it reaches the
native binding only through the `IdtxClient` engine singleton (see
[godot-binding.md](godot-binding.md)).

Two import sources are supported, both driven by the same 3-step wizard:

- **Local (res://)** — pick a USD file from the project and import it. No backend.
- **Asset server** — log in to an IDTX server, browse its files, and import a USD
  via authenticated download; optionally as a live collaboration session (create a
  new session, or join an existing one).

| Path | Contents |
|---|---|
| `plugin.gd` | `EditorPlugin` entry: main screen + inspector plugin + settings |
| `import_manager/import_manager.gd` | The wizard root (the active main screen) |
| `import_manager/step_*.gd` | The wizard steps |
| `import_manager/*_file_provider.gd`, `file_provider.gd` | Browser data sources |
| `import_manager/wizard_*.gd` | Reusable wizard widgets + theme |
| `import_manager/server_registry.gd`, `idtx_client_access.gd` | Support helpers |

---

## Plugin entry

`plugin.gd` is the `EditorPlugin`. On enter it:

- creates the import-manager main screen and attaches it to the editor's main
  viewport,
- registers the custom Inspector row for `UsdStageNode3D.stage_uri` (the "Connect"
  reload button),
- configures the ProjectSettings used to persist server URLs and usernames,
- forwards the editor's `scene_closed(filepath)` signal to the wizard so closing a
  session's scene tab is treated as an implicit *leave session* (see
  [Transient session scenes](#transient-session-scenes)),
- prunes any stray transient session scenes left by a prior crash
  (`prune_stale_session_scenes()`).

The active main screen is **`import_manager.gd`** (referenced by `plugin.gd` as its
`MainScreenScript`). The native `IdtxClient` singleton is created and registered
from C++ at module init and its `poll()` is driven by the frame ticker, so no
GDScript autoload hosts it.

---

## The import wizard (`import_manager.gd`)

The wizard is a `PanelContainer` that fills its editor tab. It holds the shared
`_import_state` (source, selected path/metadata, destination, the chosen `action`,
and the active `session_id`), swaps step controls in a step container via
`_show_step(n)`, and owns no session *protocol* state — the collaboration session
lifecycle is owned by the engine. It does track the transient scene *files* it
creates for sessions (a `_session_scenes` registry keyed by session id) so they can
be cleaned up (see [Transient session scenes](#transient-session-scenes)).

```mermaid
graph LR
    S1["Step 1<br/>select source"]
    S2A["Step 2a<br/>browse res://"]
    S2B["Step 2b<br/>browse server"]
    S3["Step 3<br/>configure + Import"]

    S1 -->|Local| S2A
    S1 -->|Server + login| S2B
    S2A --> S3
    S2B --> S3
    S3 -->|"Import: current / new"| DL["download into current / new scene"]
    S3 -->|"Import: create / join"| SESS["live session into new scene"]
```

The three live steps:

- **Step 1 — `step_select_source`** — choose *Import USD from local files* (goes
  straight to the local browser), or enter an *Asset Server* URL and click
  *Connect* to reveal an inline login panel; a successful login advances to the
  server browser.
- **Step 2a — `step_browse_files`** — browse `res://` for USD files. A thin wrapper
  around a `WizardHeader` + a `WizardFileBrowser` driven by a `LocalFileProvider` +
  a `WizardFooter`.
- **Step 2b — `step_browse_server`** — the same layout, but the `WizardFileBrowser`
  is driven by a `ServerFileProvider` that lists the backend's files.
- **Step 3 — `step_configure`** — merged import-options screen: a single 4-way
  **Import mode** radio group plus an asset preview. The **Import** button triggers
  the import. The four modes are:
  1. **Import into current scene** — download only (no session), under the selected
     node / scene root.
  2. **Import into new scene** — download only (no session), into a fresh scene.
  3. **Create collaboration session** — create + open a live session; a mode
     dropdown selects `single_edit` / `collaborative_edit`. Always a new scene.
  4. **Join collaboration session** — join a running `collaborative_edit` session for
     the selected file. Shows a list of active sessions (populated via
     `list_sessions`, with a **Refresh** button and an empty-state hint); **Import**
     stays disabled until a session is selected. Always a new scene.

  Options 3–4 are server-only (hidden for local imports).

---

## Providers (the browser data sources)

`WizardFileBrowser` is source-agnostic: it never touches `DirAccess`/`FileAccess`
or the backend directly. It asks a *provider* to list a directory and renders the
entries. The provider contract is `file_provider.gd`.

- **`LocalFileProvider`** — wraps `DirAccess`/`FileAccess` and mirrors
  `FileDialog`'s access model (`res://` / `user://` / OS filesystem). Listing is
  synchronous.
- **`ServerFileProvider`** — talks to the IDTX backend through the `IdtxClient`
  singleton (`GET /api/v1/files`). The backend returns one **flat, recursive**
  listing of every USD file (each with `filepath` + `directory`); the provider
  synthesizes a browsable directory tree from that single response, caches it, and
  reloads when the server URL changes.

---

## Reusable widgets

- **`WizardFileBrowser`** — an embeddable file browser that ports the *interior* of
  Godot's `FileDialog` into a plain `VBoxContainer` (since `FileDialog` is a
  `Window` and can't be embedded), reusing the editor theme's icons. For
  thumbnail-capable providers it also requests a thumbnail per file row and caches
  the decoded textures.
- **`WizardHeader`** — the progress line ("Step N of M — …") plus a thin progress
  bar.
- **`WizardFooter`** — the button row: `[ Back ] … [ Cancel ] [ Primary ]`.
- **`AssetDetailPanel`** — shows the selected asset's larger thumbnail preview (via
  `IdtxClient.fetch_thumbnail`) and metadata. See [flows.md](flows.md) for the full
  thumbnail request/cache behavior.
- **`wizard_theme`** — a small helper that adapts the wizard to the editor theme
  and UI scale.

---

## Support helpers

- **`ServerRegistry`** — persists known asset servers and the usernames saved for
  each, in ProjectSettings (`idtxflow/import/servers`, `idtxflow/import/last_server`),
  registered by `plugin.gd`.
- **`IdtxAccess`** (`idtx_client_access.gd`) — a stateless lookup for the native
  `IdtxClient` engine singleton, so UI scripts share one call site instead of each
  resolving the singleton themselves. It creates and owns nothing.

---

## UI import flows

Step 3's **Import mode** dispatches one of four actions (`_perform_import()`); the
end-to-end detail (backend calls and transform sync) is in [flows.md](flows.md).

- **Import into current / new scene** — the selected USD is imported into the
  current scene (`_perform_import_into_current_scene`) or a new scene
  (`_perform_import_into_new_scene`). For a local source this is entirely local; for
  a server source it is an authenticated download (no session/WebSocket).
- **Create collaboration session** — `IdtxClient.open_new_session(usd_file, mode)`
  creates the session (`POST /sessions`) and runs the engine's open-socket sequence;
  the wizard listens for `session_ready`, loads the stage from the resolved download
  URL into a new scene, and calls `attach_transform_sync(stage_node, true)`.
- **Join collaboration session** — the join list is populated on entering step 3 via
  `IdtxClient.list_sessions()` (filtered to `collaborative_edit` for the selected
  file; re-queryable with **Refresh**). Importing calls
  `IdtxClient.open_existing_session(session_id)`, which looks the session up
  (`GET /sessions/<id>`) and runs the same open-socket + `session_ready` tail as
  Create. Sessions always import into a new scene.

The session lifecycle is owned by the engine (`open_new_session` /
`open_existing_session` / `end_session`), not the wizard, so the wizard tracks no
session *protocol* state.

---

## Transient session scenes

USD sessions are not streamed — to view/edit a session the stage must be
materialized and packed into a Godot scene the editor can open. But a live session
is a *view/edit on the server*, not a project asset, so its scene must not linger.
Session imports (Create / Join) therefore save to a **hidden
`res://.idtxflow_sessions/<session_id>.tscn`** (dot-prefixed → ignored by the
FileSystem dock and the import pipeline). It cannot live under `user://` — the Godot
editor's scene loader rejects opening scenes outside the project path. (Download-only
imports still produce a normal, kept `res://<name>.tscn`.)

The wizard tracks these files in `_session_scenes` (session id → path). The transient
scene is deleted on any of:

- **Tab closed** — `EditorPlugin.scene_closed(filepath)` → `plugin.gd _on_scene_closed`
  → `import_manager.on_session_scene_closed()`, treated as an implicit **leave
  session** (`end_session()` + delete the file).
- **Cancel / session end** — `_teardown_active_session()` → `_cleanup_all_session_scenes()`.
- **Editor / plugin close** — `import_manager._exit_tree()` runs the same teardown.
- **Startup prune** — `plugin.gd _enter_tree` → `prune_stale_session_scenes()` removes
  any strays left by a prior crash where the above never fired.

The empty hidden folder is removed once the last session scene is gone. There is no
Godot API to programmatically close a specific scene tab, so on Cancel / session end
the file is deleted but an (empty) tab may remain until the user closes it.

---

## Live session indicators

While a session scene is open the plugin shows it is a **live collaboration session**
(not an ordinary local scene) through three indicators, coordinated by
`session/session_indicators.gd` (which detects the active scene, resolves the session
id, and creates/toggles the visuals) and refreshed on `scene_changed` and whenever the
tracked session set changes (`session_scenes_changed`):

- **Top banner** — a centered, mostly-solid green bar at the top of the 3D viewport
  reading "● LIVE SESSION" with the session id. Click-through so it never blocks the viewport.
- **Green viewport border** — an unfilled green rectangle around the 3D view. The banner
  and the border are the same viewport overlay `Control` — `session_viewport_overlay.gd`
  owns both — parented onto the 3D editor's internal `Node3DEditorViewportContainer`.
- **Tab title prefix + dot** — the scene tab is retitled `[remote scene] <name>` with a
  green dot icon and a tooltip (`session_tab_marker.gd`).

All three reach editor internals by class name (`Node3DEditorViewportContainer` for the
overlay, the `EditorSceneTabs` `TabBar` for the tab marker), so they are **best-effort
and version-sensitive**: if a future Godot layout change hides those nodes, each
silently no-ops (a warning is logged once) — nothing crashes, and every edit is
reversible (the overlay is freed; tab titles/icons are restored on refresh and on plugin
exit). The tab marker uses the supported `TabBar.set_tab_title` / `set_tab_icon` /
`set_tab_tooltip`. These files live under `addons/IDTXFlow/session/`.

