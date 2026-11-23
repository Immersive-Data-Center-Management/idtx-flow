@tool
extends Window

## Standalone editor controls over a registered live session and its native stage.
## The window borrows session state; closing it never ends the session.

const PRIM_TYPES := ["Xform", "Cube", "Sphere", "Cone", "Cylinder", "Capsule", "Plane"]
const GPRIM_TYPES := ["Cube", "Sphere", "Cone", "Cylinder", "Capsule", "Plane", "Mesh", "BasisCurves", "NurbsCurves", "NurbsPatch", "Points", "TetMesh"]
# Match the native post-attachment settling period before offering editing controls.
const SETTLE_FRAMES := 3

signal status_changed(message: String)

var _editor: Object
var _plugin: Object
var _client: Object
var _context: Dictionary = {}
var _selected_ids: Array[int] = []
var _generation := 0
var _closed := false
var _subscriptions: Array[Dictionary] = []
var _stage_subscriptions: Array[Dictionary] = []
var _stage_id := 0
var _loading := false
var _load_failed := false
var _loaded_evidence := false
var _settle_remaining := SETTLE_FRAMES
var _last_frame := -1
var _connection_ready := false
var _context_reason := "Open a registered live-session scene."
var _status_message := ""
# Preserve each status update, including repeated identical errors.
var _status_revision := 0
var _session_label: Label
var _target_label: Label
var _reason_label: Label
var _status_label: Label
var _type_option: OptionButton
var _create_button: Button
var _delete_button: Button
var _restore_button: Button
var _asset_button: Button
var _delete_dialog: ConfirmationDialog
var _delete_ticket: Dictionary = {}
var _asset_dialog: ConfirmationDialog
var _asset_tree: Tree
var _asset_mode: OptionButton
var _asset_status: Label
var _asset_request: HTTPRequest
var _asset_ticket: Dictionary = {}
var _asset_path := ""
var _files: Dictionary = {}
var _listing_serial := 0


## Attach to the editor, committed plugin registry and optional injected native-client interface.
func setup(editor: Object, plugin: Object, client: Object = null) -> void:
	_disconnect_all(_subscriptions)
	_observe_stage(null)
	_invalidate_actions()
	_context.clear()
	_selected_ids.clear()
	_closed = false
	_editor = editor
	_plugin = plugin
	_client = client
	if _client == null and Engine.has_singleton("IdtxClient"):
		_client = Engine.get_singleton("IdtxClient")
	_build_ui()
	if is_instance_valid(_editor) and _editor.has_method("get_selection"):
		_subscribe(_editor.call("get_selection"), "selection_changed", _on_selection_changed, _subscriptions)
	if is_instance_valid(_plugin):
		_subscribe(_plugin, "scene_changed", _on_scene_changed, _subscriptions)
		_subscribe(_plugin, "scene_closed", _on_scene_closed, _subscriptions)
		_subscribe(_coordinator(), "session_scenes_changed", _on_registry_changed, _subscriptions)
	if is_instance_valid(_client):
		_subscribe(_client, "socket_opened", _on_connection_changed, _subscriptions)
		_subscribe(_client, "snapshot_complete", _on_connection_changed, _subscriptions)
		_subscribe(_client, "session_closed", _on_session_closed, _subscriptions)
		_subscribe(_client, "ack_received", _on_ack, _subscriptions)
		_subscribe(_client, "socket_error", _on_socket_error, _subscriptions)
		_subscribe(_client, "socket_disconnected", _on_disconnected, _subscriptions)
	set_process(true)
	refresh_context()


## Return current context and independent eligibility/reasons for each action.
func get_action_state() -> Dictionary:
	_sync_context()
	return _evaluate_state()


## Capture identities and context generation for a later confirmation.
func capture_action() -> Dictionary:
	var state := get_action_state()
	if not state.target_valid:
		return {}
	return {"generation": _generation, "session_id": state.session_id, "stage_id": state.stage_id,
		"node_id": state.node_id, "prim_path": state.prim_path, "server_url": _context.get("server_url", ""),
		"source_uri": _context.get("source_uri", ""), "scene_path": _context.get("scene_path", "")}


## Submit a supported primitive under the exact current USD selection.
func submit_create(prim_type: String) -> bool:
	var state := get_action_state()
	if prim_type not in PRIM_TYPES or not state.children_allowed:
		return false
	if prim_type != "Xform" and not String(state.gprim_ancestor).is_empty():
		return false
	return _submit_request("request_create_prim", [String(state.session_id), String(state.prim_path), {"type": prim_type}],
		"Create %s under" % prim_type, String(state.prim_path))


## Submit deletion only while the original confirmation target remains current.
func submit_delete(ticket: Dictionary) -> bool:
	if not _ticket_matches(ticket) or not _evaluate_state().delete_enabled:
		return false
	var target := String(ticket.prim_path)
	var arguments := [String(ticket.session_id), target]
	_invalidate_actions()
	return _submit_request("request_delete_prim", arguments, "Delete", target)


## Submit restoration of the selected inactive prim without retargeting an ancestor.
func submit_restore(ticket: Dictionary) -> bool:
	if not _ticket_matches(ticket) or not _evaluate_state().restore_enabled:
		return false
	var target := String(ticket.prim_path)
	var arguments := [String(ticket.session_id), target]
	_invalidate_actions()
	return _submit_request("request_restore_prim", arguments, "Restore", target)


## Submit a server file as a reference or payload under the captured selection.
func submit_asset(path: String, payload: bool, ticket: Dictionary) -> bool:
	if not _ticket_matches(ticket) or not _evaluate_state().asset_enabled:
		return false
	var backend_path := _uploads_path(path)
	if backend_path.is_empty() or not _is_usd_file(backend_path):
		return false
	var target := String(ticket.prim_path)
	var arguments := [String(ticket.session_id), target, {"reference_path": backend_path, "payload": payload}]
	_invalidate_actions()
	return _submit_request("request_create_prim", arguments,
		"%s %s under" % ["Payload" if payload else "Reference", backend_path], target)


## Revalidate editor context, selection, native readiness and outstanding dialogs.
func refresh_context() -> void:
	_sync_context()
	_update_controls()


## Close owned UI and observers without changing session membership.
func close_tools() -> void:
	_cleanup()
	queue_free()


## Fetch one private listing and let the user choose a USD file and arc kind.
func open_asset_picker() -> bool:
	if not get_action_state().asset_enabled or not is_inside_tree():
		return false
	_discard_asset()
	_asset_ticket = capture_action()
	_asset_dialog = ConfirmationDialog.new()
	_asset_dialog.name = "AssetDialog"
	_asset_dialog.title = "Add asset under " + String(_asset_ticket.prim_path)
	_asset_dialog.ok_button_text = "Add asset"
	_asset_dialog.size = Vector2i(680, 500)
	add_child(_asset_dialog)
	var layout := VBoxContainer.new()
	layout.name = "AssetLayout"
	_asset_dialog.add_child(layout)
	_asset_mode = OptionButton.new()
	_asset_mode.name = "AssetMode"
	_asset_mode.add_item("Reference")
	_asset_mode.add_item("Payload")
	layout.add_child(_asset_mode)
	_asset_status = _label("AssetStatus", "Loading server files...", layout)
	_asset_tree = Tree.new()
	_asset_tree.name = "AssetTree"
	_asset_tree.custom_minimum_size = Vector2(620, 350)
	_asset_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_asset_tree.hide_root = true
	layout.add_child(_asset_tree)
	_asset_tree.item_selected.connect(_on_asset_selected)
	_asset_dialog.get_ok_button().disabled = true
	_asset_dialog.confirmed.connect(_confirm_asset)
	_asset_dialog.canceled.connect(_discard_asset)
	_asset_dialog.close_requested.connect(_discard_asset)
	_asset_request = HTTPRequest.new()
	_asset_request.name = "AssetRequest"
	_asset_request.timeout = 10.0
	# Keep the existing bearer token on its configured server, including HTTP failures.
	_asset_request.max_redirects = 0
	_asset_dialog.add_child(_asset_request)
	_listing_serial += 1
	_asset_request.request_completed.connect(_on_files_completed.bind(_asset_ticket.duplicate(true), _listing_serial))
	_asset_dialog.popup_centered()
	var headers := PackedStringArray(["Accept: application/json"])
	var token := String(_client.call("get_access_token"))
	if not token.is_empty():
		headers.append("Authorization: Bearer " + token)
	var error := _asset_request.request(String(_context.server_url) + "/api/v1/files", headers, HTTPClient.METHOD_GET)
	if error != OK:
		_asset_status.text = "Could not request server files (%s)." % error_string(error)
	return true


## Ask for confirmation of the exact selected prim and its descendants.
func begin_delete() -> bool:
	if not get_action_state().delete_enabled:
		return false
	_delete_ticket = capture_action()
	_delete_dialog.dialog_text = "Deactivate %s and its descendants in this live session?\nThe server retains their USD data for Restore." % String(_delete_ticket.prim_path)
	_delete_dialog.popup_centered()
	return true


## Read activity through the owning native stage without loading or changing USD.
func _query_activity(stage: Object, path: String) -> Dictionary:
	if stage != null and stage.has_method("get_prim_activity"):
		return stage.call("get_prim_activity", path)
	return {"status": "unavailable", "reason": "Native USD activity information is unavailable."}


func _ready() -> void:
	_build_ui()


func _process(_delta: float) -> void:
	if _closed:
		return
	_sync_context()
	var frame := Engine.get_process_frames()
	if frame != _last_frame:
		_last_frame = frame
		if _loaded_evidence and not _loading and _connection_ready and _settle_remaining > 0:
			_settle_remaining -= 1
	_update_controls()


func _exit_tree() -> void:
	_cleanup()


func _sync_context() -> void:
	if _closed:
		return
	var next: Dictionary = {}
	var ids: Array[int] = []
	var stage: Node
	_context_reason = "Open a registered live-session scene."
	if is_instance_valid(_editor) and _editor.has_method("get_edited_scene_root"):
		var edited_root: Variant = _editor.call("get_edited_scene_root")
		if is_instance_valid(edited_root): stage = edited_root as Node
		var selection: Variant = _editor.call("get_selection") if _editor.has_method("get_selection") else null
		if is_instance_valid(selection) and selection.has_method("get_selected_nodes"):
			# Editor selections can retain a freed entry until their next change notification.
			for selected: Variant in selection.call("get_selected_nodes"):
				if is_instance_valid(selected) and selected is Node: ids.append(selected.get_instance_id())
	var coordinator := _coordinator()
	if not is_instance_valid(_plugin) or not is_instance_valid(coordinator) or not coordinator.has_method("active_session_id_for_path"):
		_context_reason = "The committed IDTXFlow session registry is unavailable."
	elif not is_instance_valid(_client) or not _client.has_method("is_socket_open") or not _client.has_method("get_access_token"):
		_context_reason = "The native IDTX client is unavailable."
	elif not is_instance_valid(stage) or not stage.is_class("UsdStageNode3D"):
		_context_reason = "Select a live-session tab whose scene root is UsdStageNode3D."
	else:
		var sid := String(coordinator.call("active_session_id_for_path", stage.scene_file_path))
		if not sid.is_empty():
			next = {"session_id": sid, "stage_id": stage.get_instance_id(), "scene_path": stage.scene_file_path,
				"source_uri": String(stage.call("get_stage_uri")), "server_url": String(_client.call("get_base_url")).trim_suffix("/"),
				"token_stamp": String(_client.call("get_access_token")).sha256_text()}
			# The committed registry normally has one owner per scene path.
			if _has_property(coordinator, "_session_scenes"):
				var matches := 0
				var entries: Dictionary = coordinator.get("_session_scenes")
				for entry: Dictionary in entries.values():
					if String(entry.get("path", "")) == stage.scene_file_path: matches += 1
				if matches != 1:
					next.clear()
					_context_reason = "More than one session is registered for this scene."
	if next != _context:
		var stage_changed := int(next.get("stage_id", 0)) != _stage_id
		var source_changed := String(next.get("source_uri", "")) != String(_context.get("source_uri", ""))
		_invalidate_actions()
		_context = next
		_status_message = ""
		_observe_stage(stage if not next.is_empty() else null)
		if stage_changed:
			_loaded_evidence = false
			_loading = false
			_load_failed = false
		elif source_changed:
			# A URI change must not erase an observed pending or failed load on this node.
			_loaded_evidence = false
		_settle_remaining = SETTLE_FRAMES
		_last_frame = Engine.get_process_frames()
		_connection_ready = false
	if ids != _selected_ids:
		_invalidate_actions()
		_selected_ids = ids
	if _context.is_empty():
		return
	stage = instance_from_id(_stage_id) as Node
	if not is_instance_valid(stage) or stage.is_queued_for_deletion() or not stage.is_inside_tree():
		return
	var connected := bool(_client.call("is_socket_open", String(_context.session_id))) and bool(_client.call("is_session_synced", String(_context.session_id)))
	if connected != _connection_ready:
		_connection_ready = connected
		_settle_remaining = SETTLE_FRAMES
		_last_frame = Engine.get_process_frames()
		_invalidate_actions()
	if not _loading and not _load_failed and not _loaded_evidence and _has_live_content(stage):
		_loaded_evidence = true
		_settle_remaining = SETTLE_FRAMES
		_last_frame = Engine.get_process_frames()


func _evaluate_state() -> Dictionary:
	var state := {"ready": false, "target_valid": false, "session_id": _context.get("session_id", ""),
		"stage_id": _context.get("stage_id", 0), "node_id": 0, "prim_path": "", "reason": "",
		"children_allowed": false, "gprim_ancestor": "", "create_enabled": false, "delete_enabled": false,
		"restore_enabled": false, "asset_enabled": false, "create_reason": "", "delete_reason": "", "restore_reason": "", "asset_reason": ""}
	var stage := instance_from_id(_stage_id) as Node
	if _closed or _context.is_empty(): state.reason = _context_reason
	elif not is_instance_valid(_plugin) or not is_instance_valid(_client): state.reason = "The editor session connection is unavailable."
	elif not is_instance_valid(stage) or stage.is_queued_for_deletion() or not stage.is_inside_tree(): state.reason = "The session stage is not active in the editor."
	elif not _server_matches(): state.reason = "Return to the server that supplied this session stage."
	elif not bool(_client.call("is_socket_open", String(state.session_id))): state.reason = "The session socket is disconnected."
	elif not bool(_client.call("is_session_synced", String(state.session_id))): state.reason = "Waiting for the session snapshot."
	elif _load_failed: state.reason = "The session stage failed to load. Reload it before editing."
	elif _loading or not _loaded_evidence: state.reason = "Waiting for the native USD stage to finish loading."
	elif _settle_remaining > 0: state.reason = "Waiting for the loaded stage to settle."
	else:
		state.ready = true
		if _selected_ids.size() != 1:
			state.reason = "Select one USD prim in the scene tree."
		else:
			var node := instance_from_id(_selected_ids[0]) as Node3D
			if not is_instance_valid(node) or node == stage or node.is_queued_for_deletion() or not node.is_inside_tree() or not node.has_method("get_prim_path"):
				state.reason = "Select a converted USD prim inside this session stage."
			elif _containing_stage(node) != stage:
				state.reason = "Nested asset contents have no public mapping into this session. Select the whole asset."
			else:
				var path := String(node.call("get_prim_path"))
				state.prim_path = path
				state.node_id = node.get_instance_id()
				if not _valid_prim_path(path) or _current_prim(stage, path) != node:
					state.reason = "The selected USD representation is unavailable or ambiguous."
				else:
					var activity := _query_activity(stage, path)
					match String(activity.get("status", "unavailable")):
						"active":
							state.target_valid = true
							state.delete_enabled = true
							state.children_allowed = not node.is_class("UsdStageNode3D")
							state.gprim_ancestor = _known_gprim_ancestor(node, stage)
							state.create_enabled = state.children_allowed and (_selected_type() == "Xform" or String(state.gprim_ancestor).is_empty())
							state.asset_enabled = state.children_allowed and String(state.gprim_ancestor).is_empty()
							if not state.children_allowed: state.reason = "Whole payload assets can be deleted or restored; creation inside them is unavailable."
							elif not String(state.gprim_ancestor).is_empty(): state.reason = "Geometry ancestor %s permits Xform children only." % String(state.gprim_ancestor)
						"inactive":
							state.target_valid = true
							state.restore_enabled = int(activity.get("blocker_stage_id", 0)) == _stage_id and String(activity.get("blocker_path", "")) == path
							state.reason = "This prim is inactive. Restore selected asks the server to reactivate it."
						"ancestor_blocked":
							state.target_valid = true
							state.reason = "Inactive ancestor %s blocks this prim. Select that ancestor to restore it first." % String(activity.get("blocker_path", ""))
						_:
							state.reason = String(activity.get("reason", "USD activity is unavailable."))
	for action: String in ["create", "delete", "restore", "asset"]:
		if not bool(state[action + "_enabled"]): state[action + "_reason"] = state.reason
	if state.target_valid and not state.restore_enabled and String(state.restore_reason).is_empty():
		state.restore_reason = "The selected prim is already active."
	return state


func _ticket_matches(ticket: Dictionary) -> bool:
	if ticket.is_empty(): return false
	var current := capture_action()
	return not current.is_empty() and current == ticket


func _coordinator() -> Object:
	if not is_instance_valid(_plugin) or not _has_property(_plugin, "_session_scene_coordinator"):
		return null
	var coordinator: Variant = _plugin.get("_session_scene_coordinator")
	return coordinator as Object if is_instance_valid(coordinator) else null


func _has_property(object: Object, property: String) -> bool:
	for item: Dictionary in object.get_property_list():
		if String(item.name) == property: return true
	return false


func _server_matches() -> bool:
	var base := String(_context.get("server_url", ""))
	var source := String(_context.get("source_uri", ""))
	if not (base.begins_with("http://") or base.begins_with("https://")) or base.contains("?") or base.contains("#") or base.contains("@") or base.contains("\n") or base.contains("\r"):
		return false
	return source.begins_with(base + "/") and is_instance_valid(_client) and String(_client.call("get_base_url")).trim_suffix("/") == base


# Late launch has no bound loading getter; current native USD availability supplies initial evidence.
func _has_live_content(stage: Node) -> bool:
	var pending: Array[Node] = []
	pending.assign(stage.get_children())
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		if node.has_method("get_prim_path"):
			var path := String(node.call("get_prim_path"))
			if _valid_prim_path(path) and String(_query_activity(stage, path).get("status", "unavailable")) != "unavailable":
				return true
		if not node.is_class("UsdStageNode3D"): pending.append_array(node.get_children())
	return false


func _current_prim(stage: Node, path: String) -> Node:
	var result: Node
	var pending: Array[Node] = []
	pending.assign(stage.get_children())
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		if node.has_method("get_prim_path") and String(node.call("get_prim_path")) == path:
			if result != null: return null
			result = node
		if not node.is_class("UsdStageNode3D"): pending.append_array(node.get_children())
	return result


func _containing_stage(node: Node) -> Node:
	var parent := node.get_parent()
	while parent != null:
		if parent.is_class("UsdStageNode3D"): return parent
		parent = parent.get_parent()
	return null


func _known_gprim_ancestor(node: Node, stage: Node) -> String:
	var current := node
	while current != null and current != stage:
		if current.has_method("get_prim_type") and String(current.call("get_prim_type")) in GPRIM_TYPES:
			return String(current.call("get_prim_path"))
		current = current.get_parent()
	return ""


func _valid_prim_path(path: String) -> bool:
	return path.begins_with("/") and path != "/" and not path.ends_with("/") and not path.contains("//") and not path.contains(".") and not path.contains("{") and not path.contains("[")


func _observe_stage(stage: Node) -> void:
	var identity := stage.get_instance_id() if is_instance_valid(stage) else 0
	if identity == _stage_id: return
	_disconnect_all(_stage_subscriptions)
	_stage_id = identity
	if identity == 0: return
	_subscribe(stage, "stage_loading_started", _on_load_started.bind(identity), _stage_subscriptions)
	_subscribe(stage, "stage_loading_finished", _on_load_finished.bind(identity), _stage_subscriptions)
	_subscribe(stage, "stage_unloading", _on_load_started.bind(identity), _stage_subscriptions)
	_subscribe(stage, "prim_activity_changed", _on_activity_changed.bind(identity), _stage_subscriptions)
	_subscribe(stage, "tree_exiting", _on_load_started.bind(identity), _stage_subscriptions)
	_subscribe(stage, "tree_entered", _on_stage_entered.bind(identity), _stage_subscriptions)


func _subscribe(object: Object, signal_name: String, callback: Callable, subscriptions: Array[Dictionary]) -> void:
	if not is_instance_valid(object) or not object.has_signal(signal_name): return
	if not object.is_connected(signal_name, callback): object.connect(signal_name, callback)
	subscriptions.append({"id": object.get_instance_id(), "signal": signal_name, "callback": callback})


func _disconnect_all(subscriptions: Array[Dictionary]) -> void:
	for entry: Dictionary in subscriptions:
		var object := instance_from_id(int(entry.id))
		if is_instance_valid(object) and object.is_connected(String(entry.signal), entry.callback):
			object.disconnect(String(entry.signal), entry.callback)
	subscriptions.clear()


func _on_load_started(identity: int) -> void:
	if identity != _stage_id: return
	_loading = true
	_loaded_evidence = false
	_load_failed = false
	_settle_remaining = SETTLE_FRAMES
	_invalidate_actions()
	_update_controls()


func _on_load_finished(success: bool, identity: int) -> void:
	if identity != _stage_id: return
	_loading = false
	_load_failed = not success
	_loaded_evidence = success
	_settle_remaining = SETTLE_FRAMES
	_last_frame = Engine.get_process_frames()
	_invalidate_actions()
	refresh_context()


func _on_stage_entered(identity: int) -> void:
	if identity != _stage_id: return
	_loading = false
	_loaded_evidence = false
	_load_failed = false
	_settle_remaining = SETTLE_FRAMES
	_invalidate_actions()
	refresh_context()


func _on_activity_changed(identity: int) -> void:
	if identity != _stage_id: return
	_invalidate_actions()
	refresh_context()


func _on_selection_changed() -> void:
	refresh_context()


func _on_scene_changed(_root: Node) -> void:
	refresh_context()


func _on_scene_closed(_path: String) -> void:
	refresh_context()


func _on_registry_changed() -> void:
	refresh_context()


func _on_connection_changed(session_id: String) -> void:
	if session_id == String(_context.get("session_id", "")): refresh_context()


func _on_session_closed(session_id: String) -> void:
	if session_id != String(_context.get("session_id", "")): return
	_invalidate_actions()
	_settle_remaining = SETTLE_FRAMES
	refresh_context()


func _on_ack(session_id: String, _request_id: int, _request_kind: int, ok: bool, error: String) -> void:
	_sync_context()
	if _closed or session_id != String(_context.get("session_id", "")): return
	if not ok: _set_status("A session edit was rejected: " + error)


func _on_socket_error(session_id: String, code: String, message: String) -> void:
	if session_id == String(_context.get("session_id", "")):
		_set_status("Session error (%s): %s" % [code, message])
		refresh_context()


func _on_disconnected(session_id: String, _reason: int, _code: int, message: String) -> void:
	if session_id != String(_context.get("session_id", "")): return
	_invalidate_actions()
	_settle_remaining = SETTLE_FRAMES
	_set_status("Session disconnected: " + message)
	refresh_context()


func _invalidate_actions() -> void:
	_generation += 1
	_delete_ticket.clear()
	if is_instance_valid(_delete_dialog): _delete_dialog.hide()
	_discard_asset()


func _discard_asset() -> void:
	_listing_serial += 1
	if is_instance_valid(_asset_request): _asset_request.cancel_request()
	_asset_request = null
	if is_instance_valid(_asset_dialog):
		_asset_dialog.hide()
		_asset_dialog.queue_free()
	_asset_dialog = null
	_asset_tree = null
	_asset_mode = null
	_asset_status = null
	_asset_ticket.clear()
	_asset_path = ""
	_files.clear()


func _on_files_completed(result: int, response_code: int, _headers: PackedStringArray, body: PackedByteArray, ticket: Dictionary, serial: int) -> void:
	if _closed or serial != _listing_serial or not _ticket_matches(ticket) or not is_instance_valid(_asset_dialog): return
	if result != HTTPRequest.RESULT_SUCCESS or response_code < 200 or response_code >= 300:
		_asset_status.text = "Could not load server files (HTTP %d, result %d)." % [response_code, result]
		return
	var parsed: Variant = JSON.parse_string(body.get_string_from_utf8())
	if not parsed is Dictionary or not parsed.get("files") is Array:
		_asset_status.text = "The server returned an invalid file list."
		return
	_populate_files(parsed.files)


func _populate_files(entries: Array) -> void:
	_files.clear()
	_asset_tree.clear()
	var root := _asset_tree.create_item()
	var directories: Dictionary = {"": root}
	var listed_directories: Dictionary = {}
	for value: Variant in entries:
		if not value is Dictionary: continue
		var filepath := _uploads_path(String(value.get("filepath", "")))
		if filepath.is_empty() or not _is_usd_file(filepath) or _files.has(filepath): continue
		_files[filepath] = true
		# The submitted identifier defines its folder even if redundant directory metadata disagrees.
		listed_directories[filepath.get_base_dir()] = true
	var folder_paths: Array = listed_directories.keys()
	folder_paths.sort()
	for directory: String in folder_paths:
		_add_directory(directory, directories, root)
	var paths: Array = _files.keys()
	paths.sort()
	for path: String in paths:
		var parent := _add_directory(path.get_base_dir(), directories, root)
		var item := _asset_tree.create_item(parent)
		item.set_text(0, path.get_file())
		item.set_metadata(0, {"path": path, "directory": false})
	_asset_status.text = "Select a USD file. Directories expand without another request." if not paths.is_empty() else "No USD files are available on this server."


func _add_directory(path: String, directories: Dictionary, root: TreeItem) -> TreeItem:
	var directory := ""
	var parent := root
	for part: String in path.trim_prefix("/").split("/", false):
		directory += "/" + part
		if not directories.has(directory):
			var folder := _asset_tree.create_item(parent)
			folder.set_text(0, part + "/")
			folder.set_metadata(0, {"path": directory, "directory": true})
			directories[directory] = folder
		parent = directories[directory]
	return parent


func _on_asset_selected() -> void:
	_asset_path = ""
	var item := _asset_tree.get_selected()
	if item != null:
		var metadata: Variant = item.get_metadata(0)
		if metadata is Dictionary and not bool(metadata.get("directory", true)):
			var path := String(metadata.get("path", ""))
			if _files.has(path): _asset_path = path
	_asset_dialog.get_ok_button().disabled = _asset_path.is_empty()


func _confirm_asset() -> void:
	if not is_instance_valid(_asset_mode) or _asset_path.is_empty(): return
	submit_asset(_asset_path, _asset_mode.selected == 1, _asset_ticket.duplicate(true))


func _uploads_path(value: String) -> String:
	var path := value.strip_edges().replace("\\", "/")
	while path.begins_with("/"): path = path.substr(1)
	if path.is_empty() or path.contains(":") or path.contains("\n") or path.contains("\r") or path.contains("#") or path.contains("?"): return ""
	for component: String in path.split("/", true):
		if component.is_empty() or component == "." or component == "..": return ""
	return "/" + path


func _is_usd_file(path: String) -> bool:
	return path.get_extension().to_lower() in ["usd", "usda", "usdc", "usdz"]


func _selected_type() -> String:
	return PRIM_TYPES[_type_option.selected] if is_instance_valid(_type_option) else "Xform"


## Send an action and report submission without retaining its request ID.
func _submit_request(method: String, arguments: Array, operation: String, target: String) -> bool:
	var generation := _generation
	var status_revision := _status_revision
	var result: Variant = _client.callv(method, arguments)
	var submitted: bool = result is int and result > 0
	_sync_context()
	# Keep feedback from a context change or session error that occurred during the call.
	if _closed or generation != _generation or status_revision != _status_revision or not _evaluate_state().ready:
		return submitted
	if not submitted:
		_set_status("%s %s: request not submitted. Check session readiness and request values." % [operation, target])
		return false
	_update_controls()
	_set_status("%s %s: request submitted. The scene updates from server broadcasts." % [operation, target])
	return true


func _set_status(message: String) -> void:
	_status_revision += 1
	_status_message = message
	if is_instance_valid(_status_label): _status_label.text = message
	status_changed.emit(message)


func _update_controls() -> void:
	if not is_instance_valid(_session_label): return
	var state := _evaluate_state()
	_session_label.text = "Session: " + (String(state.session_id) if not String(state.session_id).is_empty() else "none")
	_target_label.text = "Selected USD prim: " + (String(state.prim_path) if not String(state.prim_path).is_empty() else "none")
	_reason_label.text = String(state.reason)
	_status_label.text = _status_message
	_create_button.disabled = not state.create_enabled
	_delete_button.disabled = not state.delete_enabled
	_restore_button.disabled = not state.restore_enabled
	_asset_button.disabled = not state.asset_enabled
	for pair: Array in [[_create_button, "create"], [_delete_button, "delete"], [_restore_button, "restore"], [_asset_button, "asset"]]:
		pair[0].tooltip_text = String(state[pair[1] + "_reason"])


func _build_ui() -> void:
	if is_instance_valid(_session_label): return
	title = "IDTXFlow Live Primitive Tools"
	size = Vector2i(530, 310)
	min_size = Vector2i(430, 260)
	close_requested.connect(close_tools)
	var margin := MarginContainer.new()
	margin.name = "Layout"
	add_child(margin)
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side: String in ["left", "top", "right", "bottom"]: margin.add_theme_constant_override("margin_" + side, 12)
	var layout := VBoxContainer.new()
	margin.add_child(layout)
	_session_label = _label("SessionLabel", "Session: none", layout)
	_target_label = _label("TargetLabel", "Selected USD prim: none", layout)
	var row := HBoxContainer.new()
	layout.add_child(row)
	_type_option = OptionButton.new()
	_type_option.name = "PrimType"
	for prim_type: String in PRIM_TYPES: _type_option.add_item(prim_type)
	_type_option.item_selected.connect(func(_index: int): _update_controls())
	row.add_child(_type_option)
	_create_button = _button("CreateButton", "Create child", row)
	_create_button.pressed.connect(func(): submit_create(_selected_type()))
	_asset_button = _button("AssetButton", "Add asset...", row)
	_asset_button.pressed.connect(open_asset_picker)
	var actions := HBoxContainer.new()
	layout.add_child(actions)
	_delete_button = _button("DeleteButton", "Delete selected", actions)
	_delete_button.pressed.connect(begin_delete)
	_restore_button = _button("RestoreButton", "Restore selected", actions)
	_restore_button.pressed.connect(func(): submit_restore(capture_action()))
	_reason_label = _label("ReasonLabel", "", layout)
	_status_label = _label("StatusLabel", "", layout)
	_label("UsageLabel", "Experimental editor example. Use the scene tree to select a USD prim; use existing gizmos after creation.", layout)
	_delete_dialog = ConfirmationDialog.new()
	_delete_dialog.name = "DeleteDialog"
	_delete_dialog.title = "Delete selected USD prim"
	_delete_dialog.ok_button_text = "Delete selected"
	_delete_dialog.confirmed.connect(func(): submit_delete(_delete_ticket.duplicate(true)))
	add_child(_delete_dialog)


func _label(node_name: String, text: String, parent: Node) -> Label:
	var label := Label.new()
	label.name = node_name
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	parent.add_child(label)
	return label


func _button(node_name: String, text: String, parent: Node) -> Button:
	var button := Button.new()
	button.name = node_name
	button.text = text
	parent.add_child(button)
	return button


func _cleanup() -> void:
	if _closed: return
	_closed = true
	set_process(false)
	_disconnect_all(_subscriptions)
	_disconnect_all(_stage_subscriptions)
	_invalidate_actions()
	_context.clear()
	_selected_ids.clear()
	_editor = null
	_plugin = null
	_client = null
