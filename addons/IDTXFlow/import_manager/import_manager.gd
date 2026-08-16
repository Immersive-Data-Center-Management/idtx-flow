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
##     3. step_configure      - import options: destination + settings + selected-asset
##                              preview; the "Import" button triggers the import
##
##   Asset Server
##     1. step_select_source   - enter server URL and log in (demo/demo for the mock backend)
##     2. step_browse_server   - browse the asset server file list (thin wrapper around
##                               the shared WizardFileBrowser + a ServerFileProvider that
##                               talks to the real IDTX backend via IdtxClient)
##     3. step_configure       - same merged import-options step; "Import" triggers the
##                               server import (create session + import from download URL)

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")

# Step scripts are resolved with load() at runtime to avoid parse-time
# preload dependency ordering issues when the plugin is first compiled.
const STEP_SELECT_SOURCE_PATH := "res://addons/IDTXFlow/import_manager/step_select_source.gd"
const STEP_BROWSE_FILES_PATH  := "res://addons/IDTXFlow/import_manager/step_browse_files.gd"
const STEP_BROWSE_SERVER_PATH := "res://addons/IDTXFlow/import_manager/step_browse_server.gd"
const STEP_CONFIGURE_PATH     := "res://addons/IDTXFlow/import_manager/step_configure.gd"
const SERVER_SESSION_PATH     := "res://addons/IDTXFlow/import_manager/server_session.gd"

# DEBUG_COLLAB_MODE - when true, server imports open a "collaborative_edit"
# session instead of "single_edit", so a SECOND WS client can join the SAME session and drive/observe
# transforms for §13 steps 7 (outbound) and 8 (inbound). v1 ships single_edit,
# so leave this false for normal use.
const DEBUG_COLLAB_MODE := false

# Shared wizard state.
#   "source"        : "local" or "server"
#   "selected_path" : res:// URI for local, or server-side "/..." path
#   "selected_meta" : server metadata dict (empty for local imports)
#   "destination"  : "current" (import under selected/root of current scene)
#                    or "new" (create a fresh scene with the stage as root)
var _import_state: Dictionary = {
	"source": "",
	"selected_path": "",
	"selected_meta": {},
	"destination": "current",
}

# Set to true while we've hooked into EditorSelection.selection_changed so the
# step-3 "Target:" info line updates live. Reset when leaving step 3.
var _selection_listener_connected: bool = false

var _server_session: RefCounted

var _editor_interface: EditorInterface

var _step_container: Control
var _step_select: Node
var _step_browse: Node          # local file browser
var _step_browse_server: Node   # server file browser
var _step_configure: Node       # merged import-options step (destination + settings + preview)

# Which step-2 variant is currently in use (based on chosen source).
var _active_browse_step: Node = null

# Active collaboration session id (server source only). Set when a session is
# created, cleared on teardown. Used to DELETE /api/v1/sessions/<id> and close
# the WS when the user cancels or the plugin/scene exits (§6.4).
var _active_session_id: String = ""


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
	_step_browse.next_requested.connect(_on_step2_next)

	_step_browse_server = (load(STEP_BROWSE_SERVER_PATH) as GDScript).new()
	_step_container.add_child(_step_browse_server)
	_step_browse_server.visible = false
	_step_browse_server.file_selected.connect(_on_step2_file_selected_server)
	_step_browse_server.back_requested.connect(_on_step2_back)
	_step_browse_server.cancel_requested.connect(_on_cancel)
	_step_browse_server.next_requested.connect(_on_step2_next)

	# Step 3 is the merged import-options step (destination + settings + preview).
	# Its "Import" button emits `confirm_requested`, which triggers the import.
	_step_configure = (load(STEP_CONFIGURE_PATH) as GDScript).new()
	_step_container.add_child(_step_configure)
	_step_configure.visible = false
	_step_configure.confirm_requested.connect(_on_step3_confirmed)
	_step_configure.back_requested.connect(_on_step3_back)
	_step_configure.cancel_requested.connect(_on_cancel)
	_step_configure.destination_changed.connect(_on_destination_changed)


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

	# Step 3 is the merged import-options step (destination + settings + preview).
	_step_configure.visible = index == 3

	if index == 3:
		# Populate the selected-asset preview from the current selection.
		var meta: Dictionary = _import_state.get("selected_meta", {})
		if not meta.is_empty() and _step_configure.has_method("set_selected_meta"):
			_step_configure.set_selected_meta(meta)
		elif _step_configure.has_method("set_selected_path"):
			_step_configure.set_selected_path(_import_state.get("selected_path", ""))
		# Keep the "Target:" line live while on step 3.
		_hook_selection_listener()
		_update_target_info()
	else:
		_unhook_selection_listener()


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


func _on_destination_changed(destination: String) -> void:
	_import_state["destination"] = destination


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

	if _server_session == null:
		_server_session = (load(SERVER_SESSION_PATH) as GDScript).new()
	else:
		_server_session.reset()
	_server_session.server_url = url
	_server_session.username = username
	_server_session.remember_credentials = remember
	_server_session.authenticated = true

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
	_show_step(1)


## Step 3 "Import" pressed → run the import (destination is already tracked via
## `_on_destination_changed`).
func _on_step3_confirmed() -> void:
	_perform_import()


func _on_step3_back() -> void:
	_show_step(2)


func _on_cancel() -> void:
	# Cancelling the wizard tears down any active server collaboration session.
	_teardown_active_session()
	_reset_and_go_home()


## Close the session WebSocket and DELETE the collaboration session (§6.4).
## Safe to call when no session is active (no-op). Called on wizard cancel and
## on plugin/scene exit so we don't leak sessions on the backend.
func _teardown_active_session() -> void:
	var client := _idtx()
	if client == null:
		_active_session_id = ""
		return

	# Detach transform sync + close the socket first so no further frames flush.
	if client.has_method("detach_transform_sync"):
		client.detach_transform_sync()
	if client.has_method("close_session_socket"):
		client.close_session_socket()

	# Then DELETE the session on the backend (best-effort; 404 is fine).
	if not _active_session_id.is_empty() and client.has_method("delete_session"):
		print("[IDTXFlow] [Import Manager] Tearing down session '%s'." % _active_session_id)
		client.delete_session(_active_session_id)

	_active_session_id = ""


func _reset_and_go_home() -> void:
	_import_state["source"] = ""
	_import_state["selected_path"] = ""
	_import_state["selected_meta"] = {}
	if _step_browse and _step_browse.has_method("reset"):
		_step_browse.reset()
	if _step_browse_server and _step_browse_server.has_method("reset"):
		_step_browse_server.reset()
	_show_step(1)


# --------------------------------------------------------------------------
# Import
# --------------------------------------------------------------------------

func _idtx() -> Object:
	if not Engine.has_singleton("IdtxClient"):
		return null
	return Engine.get_singleton("IdtxClient")


## Kicks off an asynchronous USD stage import and returns immediately.
## `_on_stage_loading_finished` handles the outcome when the C++ side emits
## `stage_loading_finished(success)` on the main thread.
##
## For the "server" source we first create a collaboration session
## (POST /api/v1/sessions), then import the stage from the authenticated
## download URL and open the session WebSocket. Local imports are unchanged.
func _perform_import() -> void:
	var file_path: String = _import_state.get("selected_path", "")
	if file_path.is_empty():
		push_warning("[IDTXFlow] [Import Manager] No file selected; aborting import.")
		return

	var source: String = _import_state.get("source", "")
	if source == "server":
		_perform_server_import(file_path)
	else:
		_perform_local_import(file_path)


## Local (res://) import path — unchanged behavior.
func _perform_local_import(file_path: String) -> void:
	var destination: String = _import_state.get("destination", "current")
	if destination == "new":
		_perform_import_into_new_scene(file_path)
	else:
		_perform_import_into_current_scene(file_path)


## Server import path: create a session, then import from the authenticated
## download URL and open the session WebSocket. `file_path` is the server-side
## `filepath` (e.g. "scenes/foo.usda").
func _perform_server_import(file_path: String) -> void:
	var client := _idtx()
	if client == null:
		push_error("[IDTXFlow] [Import Manager] IDTX client not available; cannot start server import.")
		return

	client.session_created.connect(_on_session_created, CONNECT_ONE_SHOT)
	client.request_failed.connect(_on_session_request_failed, CONNECT_ONE_SHOT)
	# v1 uses single_edit. DEBUG_COLLAB_MODE opens collaborative_edit instead so a
	# 2nd WS client (the E2E harness) can join the same session for §13 steps 7/8.
	var mode: String = "collaborative_edit" if DEBUG_COLLAB_MODE else "single_edit"
	if DEBUG_COLLAB_MODE:
		print("[IDTXFlow] [Import Manager] DEBUG_COLLAB_MODE on → creating '%s' session." % mode)
	client.create_session(file_path, mode)


func _on_session_created(session: Dictionary) -> void:
	var client := _idtx()
	if client and client.request_failed.is_connected(_on_session_request_failed):
		client.request_failed.disconnect(_on_session_request_failed)

	var usd_file: String = String(session.get("usd_file", _import_state.get("selected_path", "")))
	var session_id: String = String(session.get("session_id", ""))
	var ws_rel: String = String(session.get("ws_url", ""))

	# Remember the active session so we can DELETE it + close the WS on teardown.
	_active_session_id = session_id

	# Surface the session id / ws_url so it can be grabbed for the E2E harness
	# (godot-client-plan/e2e). Copy the printed --sid into the watch/send-xform
	# commands to drive §13 steps 7 (outbound) and 8 (inbound).
	print("[IDTXFlow] [Import Manager] Session created: session_id=%s  ws_url=%s  usd_file=%s"
		% [session_id, ws_rel, usd_file])
	print("[IDTXFlow] [Import Manager]   E2E: python idtx_e2e.py send-xform --sid %s --prim /World/Sphere --rot 0 45 0"
		% session_id)
	print("[IDTXFlow] [Import Manager]   E2E: python idtx_e2e.py watch --sid %s" % session_id)

	# Compute the authenticated remote stage URL; the JWT fetcher (installed on the
	# USD asset resolver) attaches the bearer token for the download.
	var stage_url: String = client.download_url(usd_file)

	# Open the session WebSocket (handshake / transform sync).
	if not session_id.is_empty() and not ws_rel.is_empty():
		var ws_full: String = client.ws_base_url() + ws_rel
		client.open_session_socket(session_id, ws_full)

	# Now import the stage from the remote URL, reusing the existing paths but with
	# a remote stage_uri instead of a local res:// path.
	var destination: String = _import_state.get("destination", "current")
	if destination == "new":
		_perform_import_into_new_scene(stage_url)
	else:
		_perform_import_into_current_scene(stage_url)


func _on_session_request_failed(op: String, http_code: int, code: String, message: String) -> void:
	if op != "create_session":
		return
	var client := _idtx()
	if client and client.session_created.is_connected(_on_session_created):
		client.session_created.disconnect(_on_session_created)
	push_error("[IDTXFlow] [Import Manager] Session create failed (%d %s): %s" % [http_code, code, message])


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


## Builds a fresh Node3D root + empty UsdStageNode3D child, parents it under
## the wizard so the C++ side sees it inside the editor SceneTree, then
## triggers the async import. The scene is opened as a new tab only after a
## successful import (see `_on_stage_loading_finished`). On failure the
## in-memory tree is discarded and nothing touches the disk or the editor.
func _perform_import_into_new_scene(file_path: String) -> void:
	var root_name: String = file_path.get_file().get_basename()
	if root_name.is_empty():
		root_name = "UsdImport"

	var new_root := Node3D.new()
	new_root.name = root_name

	var stage_node := UsdStageNode3D.new()
	new_root.add_child(stage_node)
	stage_node.owner = new_root

	# Parent under the wizard control so the node enters the editor's
	# SceneTree; UsdStageNode3D only starts loading once it is inside a tree.
	add_child(new_root)

	stage_node.stage_loading_finished.connect(
		_on_stage_loading_finished.bind(stage_node, file_path, true, new_root),
		CONNECT_ONE_SHOT
	)
	stage_node.stage_uri = file_path


## Signal handler for `stage_loading_finished`.
##
## Bound arguments:
##   stage_node   - the imported node (may already be freed on failure).
##   file_path    - USD source URI, used for logging.
##   is_new_scene - true when the import is targeting a fresh scene.
##   new_root     - only meaningful when `is_new_scene`; the in-memory root
##                  that will be packed and opened as a new tab on success,
##                  or freed on failure.
func _on_stage_loading_finished(
	success: bool,
	stage_node: Node,
	file_path: String,
	is_new_scene: bool = false,
	new_root: Node = null,
) -> void:
	if not success:
		push_error("[IDTXFlow] [Import Manager] Failed to import '%s'. Removing empty stage node." % file_path)
		if is_new_scene and is_instance_valid(new_root):
			new_root.queue_free()
		elif is_instance_valid(stage_node):
			stage_node.queue_free()
		return

	# For server imports, attach live-stage transform sync (§9.4) to the freshly
	# loaded stage node so gizmo edits broadcast and inbound broadcasts apply.
	if _import_state.get("source", "") == "server":
		var client := _idtx()
		if client and client.has_method("attach_transform_sync"):
			client.attach_transform_sync(stage_node, true)
			# Arm outbound broadcasting only after the USD-conversion transform
			# writes have settled, so they don't produce phantom broadcasts.
			_arm_sync_deferred(client)

	if is_new_scene:
		_finalize_new_scene_import(stage_node, file_path, new_root)
		return

	print("[IDTXFlow] [Import Manager] Imported '%s' as child of '%s'." % [file_path, stage_node.get_parent().name])

	_reset_and_go_home()

	if _editor_interface:
		_editor_interface.set_main_screen_editor("3D")
		var sel := _editor_interface.get_selection()
		if sel:
			sel.clear()
			sel.add_node(stage_node)
		_editor_interface.edit_node(stage_node)


## Packs the imported in-memory scene, writes it to a fresh `res://<name>.tscn`
## (with collision suffix), and opens it as a new scene tab.
func _finalize_new_scene_import(stage_node: Node, file_path: String, new_root: Node) -> void:
	# Let the C++-side deferred `set_owner` calls (from _configure_nodes_recursive)
	# settle so all imported prims have `owner == new_root` before packing.
	await get_tree().process_frame

	if not is_instance_valid(new_root):
		push_error("[IDTXFlow] [Import Manager] New-scene root was freed before packing; aborting.")
		return

	# Detach from the wizard so the packed scene doesn't include our Control.
	if new_root.get_parent() == self:
		remove_child(new_root)

	var packed := PackedScene.new()
	var pack_err := packed.pack(new_root)
	if pack_err != OK:
		push_error("[IDTXFlow] [Import Manager] PackedScene.pack failed (err=%d); aborting." % pack_err)
		new_root.queue_free()
		return

	var target_path := _next_unused_res_scene_path(new_root.name)
	var save_err := ResourceSaver.save(packed, target_path)
	if save_err != OK:
		push_error("[IDTXFlow] [Import Manager] Failed to save scene '%s' (err=%d); aborting." % [target_path, save_err])
		new_root.queue_free()
		return

	# The in-memory copy is no longer needed; the file on disk holds the state.
	new_root.queue_free()

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

	_reset_and_go_home()

	_editor_interface.set_main_screen_editor("3D")
	var new_stage_node := _find_stage_node(new_scene_root)
	if new_stage_node:
		var sel := _editor_interface.get_selection()
		if sel:
			sel.clear()
			sel.add_node(new_stage_node)
		_editor_interface.edit_node(new_stage_node)


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


## Arm the transform sync a few frames after load so the transform writes done
## during USD conversion don't trigger phantom outbound broadcasts.
func _arm_sync_deferred(client: Object) -> void:
	# Wait a couple of frames for conversion-time transform notifications to flush.
	await get_tree().process_frame
	await get_tree().process_frame
	if is_instance_valid(client) and client.has_method("arm_transform_sync"):
		client.arm_transform_sync()


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
	# Ensure any active collaboration session is torn down when the wizard leaves
	# the tree (editor closing, plugin disabled, scene change), so we don't leak
	# a session / WS on the backend (§6.4).
	_teardown_active_session()
