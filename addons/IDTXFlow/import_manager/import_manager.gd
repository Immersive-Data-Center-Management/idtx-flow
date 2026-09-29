@tool
extends PanelContainer

## Root Control for the USD Import Manager wizard.
##
## Added as a child of the editor's main screen (via plugin.gd). Uses a
## PanelContainer so the whole wizard automatically fills the parent tab.
##
## Two import sources are supported:
##
##   Local (res://)
##     1. step_select_source  - choose source
##     2. step_browse_files   - browse res:// for USD files (usd/usda/usdc/usdz)
##     3. step_configure      - import options: a 4-way "Import mode" choice +
##                              selected-asset preview; the "Import" button triggers
##                              the import
##
##   Asset Server
##     1. step_select_source   - enter server URL and log in (demo/demo for the mock backend)
##     2. step_browse_server   - browse the asset server file list (thin wrapper around
##                               the shared WizardFileBrowser + a ServerFileProvider that
##                               talks to the real IDTX backend via IdtxClient)
##     3. step_configure       - two download-only options (current/new scene), server imports
##                               expose "Create collaboration session" (single/collaborative
##                               mode) and "Join collaboration session" (pick a running
##                               collaborative session for the selected file). Both session
##                               options open a live WebSocket session and import into a new
##                               scene.

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const IdtxAccess := preload("res://addons/IDTXFlow/import_manager/idtx_client_access.gd")
const ServerRegistry := preload("res://addons/IDTXFlow/import_manager/server_registry.gd")

# Step scripts are resolved with load() at runtime to avoid parse-time
# preload dependency ordering issues when the plugin is first compiled.
const STEP_SELECT_SOURCE_PATH := "res://addons/IDTXFlow/import_manager/step_select_source.gd"
const STEP_BROWSE_FILES_PATH  := "res://addons/IDTXFlow/import_manager/step_browse_files.gd"
const STEP_BROWSE_SERVER_PATH := "res://addons/IDTXFlow/import_manager/step_browse_server.gd"
const STEP_CONFIGURE_PATH     := "res://addons/IDTXFlow/import_manager/step_configure.gd"

# Shared wizard state.
#   "source"        : "local" or "server"
#   "selected_path" : res:// URI for local, or server-side "/..." path
#   "selected_meta" : server metadata dict (empty for local imports)
#   "destination"  : "current" (import under selected/root of current scene)
#                    or "new" (create a fresh scene with the stage as root).
#                    Session actions (create/join) always force "new".
#   "action"        : the step-3 import mode — "current" / "new" /
#                     "create_session" / "join_session".
#   "session_id"    : the active session id (session imports only), captured on
#                     session_ready; used to name/track the transient scene.
var _import_state: Dictionary = {
	"source": "",
	"selected_path": "",
	"selected_meta": {},
	"destination": "current",
	"action": "current",
	"session_id": "",
}

# Transient session scenes we created, keyed by session_id -> scene path.
# See _session_scene_path (storage) and _cleanup_session_scene (teardown).
var _session_scenes: Dictionary = {}

## Emitted whenever the set of tracked session scenes changes (create / join /
## close / teardown), so the editor plugin can refresh the "live session"
## indicators (viewport border + tab marker).
signal session_scenes_changed

# Set to true while we've hooked into EditorSelection.selection_changed so the
# step-3 "Target:" info line updates live. Reset when leaving step 3.
var _selection_listener_connected: bool = false

var _editor_interface: EditorInterface

var _step_container: Control
var _step_select: Node
var _step_browse: Node          # local file browser
var _step_browse_server: Node   # server file browser
var _step_configure: Node       # merged import-options step (destination + settings + preview)

# Which step-2 variant is currently in use (based on chosen source).
var _active_browse_step: Node = null

# Active collaboration session id and lifecycle are owned by the engine
# (open_new_session / open_existing_session / end_session), so the wizard tracks
# no session state.


func set_editor_interface(editor_interface: EditorInterface) -> void:
	_editor_interface = editor_interface
	# Match the editor's own UI scale (e.g. 100%, 150%, 200%).
	if editor_interface and editor_interface.has_method("get_editor_scale"):
		WizardTheme.editor_scale = editor_interface.get_editor_scale()


func _init() -> void:
	name = "IDTXFlowImportManager"
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_build_ui()
	_show_step(1)


func _build_ui() -> void:
	var root_vb := VBoxContainer.new()
	root_vb.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root_vb.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root_vb.add_theme_constant_override("separation", WizardTheme.px(10))
	add_child(root_vb)

	root_vb.add_child(_build_title_bar())

	_step_container = VBoxContainer.new()
	_step_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_step_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root_vb.add_child(_step_container)

	_step_select = (load(STEP_SELECT_SOURCE_PATH) as GDScript).new()
	var _last_server := ServerRegistry.last_server()
	_step_select._default_url = _last_server if not _last_server.is_empty() else "http://localhost:8080"
	_step_container.add_child(_step_select)
	_step_select.visible = false
	_step_select.local_files_requested.connect(_on_step1_local_files)
	_step_select.server_login_succeeded.connect(_on_step1_server_login)
	_step_select.cancel_requested.connect(_on_cancel)

	_step_browse = (load(STEP_BROWSE_FILES_PATH) as GDScript).new()
	_step_container.add_child(_step_browse)
	_step_browse.visible = false
	_step_browse.file_selected.connect(_on_step2_file_selected_local)
	_step_browse.back_requested.connect(_on_step2_back)
	_step_browse.cancel_requested.connect(_on_cancel)
	_step_browse.confirm_requested.connect(_on_step2_next)

	_step_browse_server = (load(STEP_BROWSE_SERVER_PATH) as GDScript).new()
	_step_container.add_child(_step_browse_server)
	_step_browse_server.visible = false
	_step_browse_server.file_selected.connect(_on_step2_file_selected_server)
	_step_browse_server.back_requested.connect(_on_step2_back)
	_step_browse_server.cancel_requested.connect(_on_cancel)
	_step_browse_server.confirm_requested.connect(_on_step2_next)

	# Step 3 is the 4-way import mode + preview.
	# Its "Import" button emits `confirm_requested`, which triggers the import.
	_step_configure = (load(STEP_CONFIGURE_PATH) as GDScript).new()
	_step_container.add_child(_step_configure)
	_step_configure.visible = false
	_step_configure.confirm_requested.connect(_on_step3_confirmed)
	_step_configure.back_requested.connect(_on_step3_back)
	_step_configure.cancel_requested.connect(_on_cancel)
	_step_configure.refresh_sessions_requested.connect(_refresh_join_sessions)


func _build_title_bar() -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", WizardTheme.px(6))

	var icon_rect := TextureRect.new()
	var s := WizardTheme.px(18)
	icon_rect.custom_minimum_size = Vector2(s, s)
	icon_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	var icon := WizardTheme.get_editor_icon(self, "Filesystem", "Load")
	if icon:
		icon_rect.texture = icon
	icon_rect.modulate = WizardTheme.get_accent_color(self)
	row.add_child(icon_rect)

	var title := Label.new()
	title.text = "USD Importer"
	title.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_TITLE))
	row.add_child(title)

	return row


# --------------------------------------------------------------------------
# Step navigation
# --------------------------------------------------------------------------

func _show_step(index: int) -> void:
	_step_select.visible = index == 1

	# Step 2: one of two browsers, depending on chosen source.
	var use_server: bool = _import_state.get("source", "") == "server"
	_step_browse.visible        = index == 2 and not use_server
	_step_browse_server.visible = index == 2 and use_server
	_active_browse_step = _step_browse_server if use_server else _step_browse

	# Step 3 is the merged import-options step (4-way import mode + preview).
	_step_configure.visible = index == 3

	if index == 3:
		var is_server: bool = _import_state.get("source", "") == "server"
		# Populate the selected-asset preview from the current selection.
		var meta: Dictionary = _import_state.get("selected_meta", {})
		if not meta.is_empty() and _step_configure.has_method("set_selected_meta"):
			_step_configure.set_selected_meta(meta)
		elif _step_configure.has_method("set_selected_path"):
			_step_configure.set_selected_path(_import_state.get("selected_path", ""))
		# Create/Join session options are server-only; hide them for local imports.
		if _step_configure.has_method("set_server_options_visible"):
			_step_configure.set_server_options_visible(is_server)
		# For server imports, fetch active collaborative sessions for this file so
		# the "Join" option can list them.
		if is_server:
			_refresh_join_sessions()
		# Keep the "Target:" line live while on step 3.
		_hook_selection_listener()
		_update_target_info()
	else:
		_unhook_selection_listener()


# --------------------------------------------------------------------------
# Step 3 join-session list
# --------------------------------------------------------------------------

## Fetch active sessions and hand the "Join" option only the collaborative_edit
## ones for the currently selected USD path.
func _refresh_join_sessions() -> void:
	var client := _idtx()
	if client == null or not client.has_method("list_sessions"):
		if _step_configure.has_method("set_join_sessions"):
			_step_configure.set_join_sessions([])
		return
	client.list_sessions(_on_join_sessions_listed)


func _on_join_sessions_listed(result: Dictionary) -> void:
	var sessions: Array = []
	if bool(result.get("ok", false)):
		var selected_path: String = _import_state.get("selected_path", "")
		for s in result.get("result", []):
			if String(s.get("mode", "")) != "collaborative_edit":
				continue
			# Only sessions for the file the operator selected.
			if String(s.get("usd_file", "")) != selected_path:
				continue
			sessions.append(s)
	else:
		push_warning("[IDTXFlow] [Import Manager] Could not list sessions: %s"
			% String(result.get("message", "")))
	if _step_configure.has_method("set_join_sessions"):
		# Pass the sessions we are already in so the list can gray them out.
		var joined := PackedStringArray()
		for sid in _session_scenes.keys():
			joined.append(sid)
		_step_configure.set_join_sessions(sessions, joined)


# --------------------------------------------------------------------------
# Step 3 target-info + destination handling
# --------------------------------------------------------------------------

func _hook_selection_listener() -> void:
	if _selection_listener_connected or _editor_interface == null:
		return
	var sel := _editor_interface.get_selection()
	if sel:
		sel.selection_changed.connect(_update_target_info)
		_selection_listener_connected = true


func _unhook_selection_listener() -> void:
	if not _selection_listener_connected or _editor_interface == null:
		return
	var sel := _editor_interface.get_selection()
	if sel and sel.selection_changed.is_connected(_update_target_info):
		sel.selection_changed.disconnect(_update_target_info)
	_selection_listener_connected = false


## Pushes the current "target scene node" info (or an "open a scene" hint) to
## the merged import-options step so it can update the label under the
## "Import into current scene" radio.
func _update_target_info() -> void:
	if _step_configure == null or not _step_configure.has_method("set_current_target_info"):
		return

	if _editor_interface == null:
		_step_configure.set_current_target_info("", "", false)
		return

	var scene_root := _editor_interface.get_edited_scene_root()
	if scene_root == null:
		_step_configure.set_current_target_info("", "", false)
		return

	var target := _get_target_scene_node()
	if target == null:
		_step_configure.set_current_target_info(scene_root.name, "root", true)
		return

	var sub: String
	if target == scene_root:
		sub = "root"
	else:
		sub = String(scene_root.get_path_to(target))
	_step_configure.set_current_target_info(target.name, sub, true)


# --------------------------------------------------------------------------
# Step signal handlers
# --------------------------------------------------------------------------

func _on_step1_local_files() -> void:
	_import_state["source"] = "local"
	_import_state["selected_meta"] = {}
	if _step_browse.has_method("reset"):
		_step_browse.reset()
	_show_step(2)


func _on_step1_server_login(url: String, username: String, remember: bool) -> void:
	_import_state["source"] = "server"
	_import_state["selected_meta"] = {}

	if remember:
		ServerRegistry.add_entry(url, username)
	
	if _step_browse_server.has_method("set_server_url"):
		_step_browse_server.set_server_url(url)
	if _step_browse_server.has_method("reset"):
		_step_browse_server.reset()

	_show_step(2)


func _on_step2_file_selected_local(path: String) -> void:
	_import_state["selected_path"] = path
	_import_state["selected_meta"] = {}


func _on_step2_file_selected_server(path: String, meta: Dictionary) -> void:
	_import_state["selected_path"] = path
	_import_state["selected_meta"] = meta


func _on_step2_next() -> void:
	# Pull the latest selection from whichever browse step is active.
	if _active_browse_step and _active_browse_step.has_method("get_selected_path"):
		var p: String = _active_browse_step.get_selected_path()
		if not p.is_empty():
			_import_state["selected_path"] = p
	if _active_browse_step and _active_browse_step.has_method("get_selected_meta"):
		_import_state["selected_meta"] = _active_browse_step.get_selected_meta()
	_show_step(3)


func _on_step2_back() -> void:
	if _step_select and _step_select.has_method("reset_login"):
		_step_select.reset_login()
	_show_step(1)


## Step 3 "Import" pressed → run the import for the selected action.
func _on_step3_confirmed() -> void:
	_perform_import()


func _on_step3_back() -> void:
	_show_step(2)


func _on_cancel() -> void:
	_reset_and_go_home()


## Leave ONE collaboration session: tell the engine to end that session (detach
## its stage + close its socket) and delete its transient scene. Safe when the id
## is unknown. This is the per-tab "leave" used when a single session scene closes.
func _leave_session(session_id: String) -> void:
	if session_id.is_empty():
		return
	var client := _idtx()
	if client and client.has_method("end_session"):
		client.end_session(session_id)
	_cleanup_session_scene(session_id)


## Tear down EVERY active session. Ends each session in the engine and deletes all transient scenes.
func _teardown_all_sessions() -> void:
	var client := _idtx()
	for sid in _session_scenes.keys().duplicate():
		if client and client.has_method("end_session"):
			client.end_session(sid)
	_cleanup_all_session_scenes()


## Delete the transient scene file for a tracked session. `session_id` must be a
## tracked id. The scene may still be open as an editor tab; there is no API to
## programmatically close a specific tab, so we delete the file and leave any
## empty tab for the user to close
func _cleanup_session_scene(session_id: String) -> void:
	if not _session_scenes.has(session_id):
		return
	var path: String = _session_scenes[session_id]
	_session_scenes.erase(session_id)
	session_scenes_changed.emit()
	if FileAccess.file_exists(path):
		var err := DirAccess.remove_absolute(path)
		print("[IDTXFlow] [Import Manager] Removed transient session scene '%s' (err=%d)." % [path, err])
	_remove_session_dir_if_empty()


## Delete every tracked transient session scene (teardown / editor close).
func _cleanup_all_session_scenes() -> void:
	for sid in _session_scenes.keys().duplicate():
		_cleanup_session_scene(sid)


## True when `path` is one of the transient session scenes we created (used by the
## editor plugin to decide whether to show the "live session" indicators).
func is_session_scene_path(path: String) -> bool:
	return _session_scenes.values().has(path)


## The session id backing the transient scene at `path`, or "" if none.
func active_session_id_for_path(path: String) -> String:
	for sid in _session_scenes:
		if _session_scenes[sid] == path:
			return sid
	return ""


## A tracked session scene's editor tab was closed by the user (EditorPlugin
## scene_closed). Treat it as an implicit "leave session": tear the session down
## and delete the transient file. Ignores non-session paths.
func on_session_scene_closed(filepath: String) -> void:
	for sid in _session_scenes.keys().duplicate():
		if _session_scenes[sid] == filepath:
			print("[IDTXFlow] [Import Manager] Session scene tab closed; leaving session '%s'." % sid)
			_leave_session(sid)
			return


## Remove the hidden session-scene directory if it is now empty, so we don't
## leave an empty folder behind in the project.
func _remove_session_dir_if_empty() -> void:
	if not _session_scenes.is_empty():
		return
	var dir := DirAccess.open("res://.idtxflow_sessions")
	if dir == null:
		return
	if dir.get_files().is_empty() and dir.get_directories().is_empty():
		DirAccess.remove_absolute("res://.idtxflow_sessions")


## Delete any stray transient session scenes left by a prior crash / hard close
## where scene_closed / _exit_tree never fired. Called once at plugin startup.
func prune_stale_session_scenes() -> void:
	var dir := DirAccess.open("res://.idtxflow_sessions")
	if dir == null:
		return
	for f in dir.get_files():
		if f.ends_with(".tscn"):
			DirAccess.remove_absolute("res://.idtxflow_sessions/%s" % f)
	_remove_session_dir_if_empty()


func _reset_and_go_home() -> void:
	_import_state["source"] = ""
	_import_state["selected_path"] = ""
	_import_state["selected_meta"] = {}
	if _step_browse and _step_browse.has_method("clear_selection"):
		_step_browse.clear_selection()
	if _step_browse_server and _step_browse_server.has_method("clear_selection"):
		_step_browse_server.clear_selection()
	if _step_select and _step_select.has_method("reset_login"):
		_step_select.reset_login()
	_show_step(1)


# --------------------------------------------------------------------------
# Import
# --------------------------------------------------------------------------

func _idtx() -> Object:
	return IdtxAccess.get_client()


## Kicks off an asynchronous USD stage import and returns immediately.
## `_on_stage_loading_finished` handles the outcome when the C++ side emits
## `stage_loading_finished(success)` on the main thread.
##
## Resolves the 4-way import action chosen in step 3 and routes accordingly.
## Session actions (create/join) always import into a NEW scene.
func _perform_import() -> void:
	var file_path: String = _import_state.get("selected_path", "")
	if file_path.is_empty():
		push_warning("[IDTXFlow] [Import Manager] No file selected; aborting import.")
		return

	var action: String = "current"
	if _step_configure.has_method("get_import_action"):
		action = _step_configure.get_import_action()
	_import_state["action"] = action

	# Download-only actions (Options 1/2) carry their own destination; session
	# actions (Options 3/4) force a new scene.
	match action:
		"new":
			_import_state["destination"] = "new"
		"create_session", "join_session":
			_import_state["destination"] = "new"
		_:
			_import_state["destination"] = "current"

	var source: String = _import_state.get("source", "")
	match action:
		"create_session":
			_perform_server_session_import(file_path)
		"join_session":
			_perform_server_join_import()
		_:
			# Plain download (current/new), server or local.
			if source == "server":
				_perform_server_download_import(file_path)
			else:
				_perform_local_import(file_path)


## Local (res://) import path — unchanged behavior.
func _perform_local_import(file_path: String) -> void:
	var destination: String = _import_state.get("destination", "current")
	if destination == "new":
		_perform_import_into_new_scene(file_path)
	else:
		_perform_import_into_current_scene(file_path)


## Default server import: fetch the file over the authenticated download URL and
## load it exactly like a local import (no session, no WebSocket). The USD stage
## node fetches the URL via the JWT-injecting asset resolver, so we only need the
## resolved URL and a valid login.
func _perform_server_download_import(file_path: String) -> void:
	var client := _idtx()
	if client == null:
		push_error("[IDTXFlow] [Import Manager] IDTX client not available; cannot download.")
		return
	if not client.is_authenticated():
		push_error("[IDTXFlow] [Import Manager] Not authenticated; log in before importing.")
		return

	var url: String = client.download_url(file_path)
	if url.is_empty():
		push_error("[IDTXFlow] [Import Manager] Could not resolve download URL for '%s'." % file_path)
		return

	var destination: String = _import_state.get("destination", "current")
	if destination == "new":
		_perform_import_into_new_scene(url)
	else:
		_perform_import_into_current_scene(url)


## Collaboration server import (Option 3: Create). Create a new session, then
## import from the authenticated download URL and open the session WebSocket. The
## session mode (single_edit / collaborative_edit) is chosen in step 3.
func _perform_server_session_import(file_path: String) -> void:
	var client := _idtx()
	if client == null:
		push_error("[IDTXFlow] [Import Manager] IDTX client not available; cannot start server import.")
		return

	client.session_ready.connect(_on_session_ready, CONNECT_ONE_SHOT)
	var mode: String = _step_configure.get_session_mode()
	print("[IDTXFlow] [Import Manager] Creating '%s' session." % mode)
	# The engine owns the create → open-socket sequence: the create result comes
	# back through the completion; the resolved stage download URL then arrives on
	# `session_ready`.
	client.open_new_session(file_path, mode, _on_import_create_done)


func _on_import_create_done(result: Dictionary) -> void:
	if bool(result.get("ok", false)):
		return
	var client := _idtx()
	if client and client.session_ready.is_connected(_on_session_ready):
		client.session_ready.disconnect(_on_session_ready)
	push_error("[IDTXFlow] [Import Manager] Session create failed (%d %s): %s"
		% [int(result.get("http_code", 0)), String(result.get("error_code", "")), String(result.get("message", ""))])


## Collaboration server import (Option 4: Join). Join a running session picked in
## step 3, then import from the authenticated download URL the engine resolves
## and open the session WebSocket. Shares the `session_ready` handler with Create.
func _perform_server_join_import() -> void:
	var client := _idtx()
	if client == null:
		push_error("[IDTXFlow] [Import Manager] IDTX client not available; cannot join session.")
		return

	var session_id: String = ""
	if _step_configure.has_method("get_selected_session_id"):
		session_id = _step_configure.get_selected_session_id()
	if session_id.is_empty():
		push_warning("[IDTXFlow] [Import Manager] No session selected to join; aborting.")
		return

	# Client-side duplicate-join guard: we cannot join a session we are already in.
	# Friendly message and avoids opening a redundant flow. See also step_configure's grayed-out "joined" rows.
	if _session_scenes.has(session_id):
		push_warning("[IDTXFlow] [Import Manager] Already in session '%s'; not joining again." % session_id)
		return

	client.session_ready.connect(_on_session_ready, CONNECT_ONE_SHOT)
	print("[IDTXFlow] [Import Manager] Joining session '%s'." % session_id)
	# The engine owns the lookup → open-socket sequence; the resolved stage download URL then arrives on `session_ready`.
	client.open_existing_session(session_id, _on_import_join_done)


func _on_import_join_done(result: Dictionary) -> void:
	if bool(result.get("ok", false)):
		return
	var client := _idtx()
	if client and client.session_ready.is_connected(_on_session_ready):
		client.session_ready.disconnect(_on_session_ready)
	push_error("[IDTXFlow] [Import Manager] Session join failed (%d %s): %s"
		% [int(result.get("http_code", 0)), String(result.get("error_code", "")), String(result.get("message", ""))])


func _on_session_ready(session: Dictionary, stage_url: String) -> void:
	# The engine already created the session and opened its socket; the id/ws_url
	# are owned by the engine (torn down via end_session). Import the stage from
	# the authenticated download URL the engine resolved.
	var sid: String = session.get("session_id", "")
	var ws_url: String = session.get("ws_url", "")
	print("[IDTXFlow] [Import Manager] Session created: session_id=%s  ws_url=%s" % [sid, ws_url])
	# Remember the id so the transient scene can be named/tracked and torn down.
	_import_state["session_id"] = sid

	var destination: String = _import_state.get("destination", "current")
	if destination == "new":
		_perform_import_into_new_scene(stage_url)
	else:
		_perform_import_into_current_scene(stage_url)


func _perform_import_into_current_scene(file_path: String) -> void:
	var target_node: Node = _get_target_scene_node()
	if target_node == null:
		push_warning("[IDTXFlow] [Import Manager] No target scene node available; open a scene first.")
		return

	var stage_node := UsdStageNode3D.new()
	target_node.add_child(stage_node)

	var scene_root: Node = null
	if _editor_interface:
		scene_root = _editor_interface.get_edited_scene_root()
	stage_node.owner = scene_root if scene_root != null else target_node

	stage_node.stage_loading_finished.connect(
		_on_stage_loading_finished.bind(stage_node, file_path, false),
		CONNECT_ONE_SHOT
	)
	stage_node.stage_uri = file_path


## Builds a fresh empty UsdStageNode3D root node, then
## triggers the async import. The scene is opened as a new tab only after a
## successful import (see `_on_stage_loading_finished`). On failure the
## in-memory tree is discarded and nothing touches the disk or the editor.
func _perform_import_into_new_scene(file_path: String) -> void:
	
	var stage_node := UsdStageNode3D.new()
	# Parent under the wizard control so the node enters the editor's
	# SceneTree; UsdStageNode3D only starts loading once it is inside a tree.
	add_child(stage_node)
	# This node becomes the packed-scene root in _finalize_new_scene_import, so itmust have no owner
	stage_node.owner = null

	stage_node.stage_loading_finished.connect(
		_on_stage_loading_finished.bind(stage_node, file_path, true),
		CONNECT_ONE_SHOT
	)
	stage_node.stage_uri = file_path


## Signal handler for `stage_loading_finished`.
##
## Bound arguments:
##   stage_node   - the imported node (may already be freed on failure).
##   file_path    - USD source URI, used for logging.
##   is_new_scene - true when the import is targeting a fresh scene.
func _on_stage_loading_finished(
	success: bool,
	stage_node: Node,
	file_path: String,
	is_new_scene: bool = false,
) -> void:
	if not success:
		push_error("[IDTXFlow] [Import Manager] Failed to import '%s'. Removing empty stage node." % file_path)
		if is_instance_valid(stage_node):
			stage_node.queue_free()
		return

	if is_new_scene:
		_finalize_new_scene_import(stage_node, file_path)
		return

	# For server imports, attach live-stage transform sync to the freshly
	# loaded stage node so gizmo edits broadcast and inbound broadcasts apply.
	# Outbound broadcasting auto-arms in the engine a few frames after attach, so
	# the USD-conversion transform writes don't produce phantom broadcasts.
	# in the 'is_new_scene' scenario this will be handled within '_finalize_new_scene_import'
	if _import_state.get("source", "") == "server":
		var client := _idtx()
		if client and client.has_method("attach_transform_sync"):
			client.attach_transform_sync(_import_state.get("session_id", ""), stage_node, true)

	print("[IDTXFlow] [Import Manager] Imported '%s' as child of '%s'." % [file_path, stage_node.get_parent().name])

	if _editor_interface:
		_editor_interface.set_main_screen_editor("3D")
		var sel := _editor_interface.get_selection()
		if sel:
			sel.clear()
			sel.add_node(stage_node)
		_editor_interface.edit_node(stage_node)

	_reset_and_go_home()


## Packs the imported in-memory scene, writes it to a fresh `res://<name>.tscn`
## (with collision suffix), and opens it as a new scene tab.
func _finalize_new_scene_import(stage_node: Node, file_path: String) -> void:
	# Let the C++-side deferred `set_owner` calls (from _configure_nodes_recursive)
	# settle so all imported prims have `owner == new_root` before packing.
	await get_tree().process_frame

	if not is_instance_valid(stage_node):
		push_error("[IDTXFlow] [Import Manager] New-scene root was freed before packing; aborting.")
		return

	# Detach from the wizard so the packed scene doesn't include our Control.
	if stage_node.get_parent() == self:
		remove_child(stage_node)

	var packed := PackedScene.new()
	var pack_err := packed.pack(stage_node)
	if pack_err != OK:
		push_error("[IDTXFlow] [Import Manager] PackedScene.pack failed (err=%d); aborting." % pack_err)
		stage_node.queue_free()
		return

	# Session imports (create/join) get a transient scene; downloads get a kept
	# res:// asset. See _session_scene_path / _cleanup_session_scene.
	var is_session := _is_session_import()
	var session_id: String = _import_state.get("session_id", "")
	var target_path: String
	if is_session:
		target_path = _session_scene_path(session_id, stage_node.name)
	else:
		target_path = _next_unused_res_scene_path(stage_node.name)

	var save_err := ResourceSaver.save(packed, target_path)
	if save_err != OK:
		push_error("[IDTXFlow] [Import Manager] Failed to save scene '%s' (err=%d); aborting." % [target_path, save_err])
		stage_node.queue_free()
		return

	# The in-memory copy is no longer needed; the file on disk holds the state.
	stage_node.queue_free()

	if _editor_interface == null:
		push_warning("[IDTXFlow] [Import Manager] No editor interface; scene saved at '%s'." % target_path)
		return

	_editor_interface.open_scene_from_path(target_path)
	await get_tree().process_frame

	var new_scene_root := _editor_interface.get_edited_scene_root()
	if new_scene_root == null:
		push_error("[IDTXFlow] [Import Manager] Failed to open new scene tab from '%s'." % target_path)
		return

	print("[IDTXFlow] [Import Manager] Imported '%s' as '%s'." % [file_path, target_path])

	# Track transient session scenes so they can be cleaned up on scene_closed /
	# teardown / editor close.
	if is_session and not session_id.is_empty():
		_session_scenes[session_id] = target_path
		session_scenes_changed.emit()

	_editor_interface.set_main_screen_editor("3D")
	var new_stage_node := _find_stage_node(new_scene_root)
	if new_stage_node:
		# In a live collaboration session the stage root is scene placement only
		# (it is never synced — only its prims are). Lock it from manual gizmo drags
		# so users don't accidentally move the whole stage during a session. This is
		# Godot's editor per-node lock (the Scene-dock padlock): it blocks manual
		# selection/drag but leaves programmatic set_transform untouched.
		if _is_session_import():
			new_stage_node.set_meta("_edit_lock_", true)
		var sel := _editor_interface.get_selection()
		if sel:
			sel.clear()
			sel.add_node(new_stage_node)
		_editor_interface.edit_node(new_stage_node)
		# The initially loaded stage node is long gone, so we need to re-wire the sync
		# for the now existing UsdStageNode3D. Otherwise we will not be able to handle
		# inbound transform changes properly
		if _import_state.get("source", "") == "server":
			var client := _idtx()
			if client and client.has_method("attach_transform_sync"):
				client.attach_transform_sync(_import_state.get("session_id", ""), new_stage_node, true)

	_reset_and_go_home()


## True for a live collaboration session import (create/join). See
## _session_scene_path for how session scenes are stored and cleaned up.
func _is_session_import() -> bool:
	var action: String = _import_state.get("action", "")
	return action == "create_session" or action == "join_session"


## Path for a transient session-backed scene. Lives in a hidden
## res://.idtxflow_sessions/ folder (dot-prefixed → ignored by the FileSystem
## dock and the import pipeline) so it is never treated as a project asset; the
## folder is created on demand and removed when the last session scene is gone.
func _session_scene_path(session_id: String, basename: String) -> String:
	var dir := "res://.idtxflow_sessions"
	DirAccess.make_dir_recursive_absolute(dir)
	var stem: String = session_id
	if stem.is_empty():
		stem = basename if not basename.is_empty() else "UsdSession"
	# Sanitize the id into a filesystem-safe stem.
	stem = stem.validate_filename()
	return "%s/%s.tscn" % [dir, stem]


## Returns the first path of the form `res://<basename>.tscn` (or `_1`, `_2`,
## ...) that does not exist on disk yet, so we don't overwrite user files.
func _next_unused_res_scene_path(basename: String) -> String:
	var stem: String = basename
	if stem.is_empty():
		stem = "UsdImport"
	var candidate := "res://%s.tscn" % stem
	if not FileAccess.file_exists(candidate):
		return candidate
	var i := 1
	while true:
		candidate = "res://%s_%d.tscn" % [stem, i]
		if not FileAccess.file_exists(candidate):
			return candidate
		i += 1
	return candidate


## Depth-first search for the first UsdStageNode3D descendant of `root`.
func _find_stage_node(root: Node) -> Node:
	if root == null:
		return null
	if root.get_class() == "UsdStageNode3D":
		return root
	for i in root.get_child_count():
		var found := _find_stage_node(root.get_child(i))
		if found:
			return found
	return null


func _get_target_scene_node() -> Node:
	if _editor_interface == null:
		return null

	var selection := _editor_interface.get_selection()
	if selection:
		var nodes := selection.get_selected_nodes()
		if not nodes.is_empty():
			return nodes[0]

	return _editor_interface.get_edited_scene_root()

func _exit_tree() -> void:
	_unhook_selection_listener()
	# Ensure every active collaboration session is torn down when the wizard leaves
	# the tree (editor closing, plugin disabled, scene change), so we don't leak
	# a session / WS on the backend.
	_teardown_all_sessions()
