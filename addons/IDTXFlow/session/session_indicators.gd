@tool
extends RefCounted

## Coordinator for the editor's "live collaboration session" visual indicators.
##
## Unifies (drives) all session visual representation:
##   - a green border overlay around the 3D viewport + a centered banner bar
##   - a scene-tab title prefix + dot marker

const SessionTabMarker := preload("res://addons/IDTXFlow/session/session_tab_marker.gd")
const SessionViewportOverlay := preload("res://addons/IDTXFlow/session/session_viewport_overlay.gd")

const LIVE_SESSION_COLOR := Color(0.24, 0.82, 0.36)

var _editor_interface: EditorInterface
var _main_screen: Node = null   # the wizard; source of is_session_scene_path()

var _tab_marker: RefCounted = null
var _viewport_overlay: Control = null   # owns both the border and the top banner


func setup(editor_interface: EditorInterface, main_screen: Node) -> void:
	_editor_interface = editor_interface
	_main_screen = main_screen
	_tab_marker = SessionTabMarker.new()
	_tab_marker.setup(editor_interface)


## Recompute active state for the current scene and update every indicator.
func refresh() -> void:
	var active := _current_scene_is_session()

	# Viewport overlay (border + banner, best-effort): create lazily, then toggle.
	if active and _viewport_overlay == null:
		_create_viewport_overlay()
	if _viewport_overlay != null and is_instance_valid(_viewport_overlay):
		_viewport_overlay.visible = active
		if active:
			_viewport_overlay.set_session_label(_current_session_label())
		_viewport_overlay.queue_redraw()

	if _tab_marker != null:
		_tab_marker.refresh(_session_scene_paths())


## Remove all indicators, leaving no residue.
func clear() -> void:
	if _viewport_overlay != null and is_instance_valid(_viewport_overlay):
		_viewport_overlay.queue_free()
	_viewport_overlay = null
	if _tab_marker != null:
		_tab_marker.clear()


# ---------------------------------------------------------------------------
# State detection
# ---------------------------------------------------------------------------

## True when the current edited scene is a live session.
func _current_scene_is_session() -> bool:
	if _main_screen == null or not _main_screen.has_method("is_session_scene_path"):
		return false
	var root := _editor_interface.get_edited_scene_root()
	if root == null:
		return false
	return _main_screen.is_session_scene_path(root.scene_file_path)


## The tracked session scene paths from the wizard (empty if unavailable).
func _session_scene_paths() -> PackedStringArray:
	var paths := PackedStringArray()
	if _main_screen == null or not _main_screen.has_method("is_session_scene_path"):
		return paths
	for p in _editor_interface.get_open_scenes():
		if _main_screen.is_session_scene_path(p):
			paths.append(p)
	return paths


# ---------------------------------------------------------------------------
# Viewport border overlay
# ---------------------------------------------------------------------------

func _create_viewport_overlay() -> void:
	var container := _find_node3d_viewport_container()
	if container == null:
		return
	var overlay := SessionViewportOverlay.new()
	overlay.name = "IDTXFlowSessionBorder"
	overlay.border_color = LIVE_SESSION_COLOR
	container.add_child(overlay)
	_viewport_overlay = overlay


## Human-readable label for the current session: its session id, or the scene
## basename as a fallback.
func _current_session_label() -> String:
	var root := _editor_interface.get_edited_scene_root()
	if root == null:
		return ""
	var path := root.scene_file_path
	if _main_screen != null and _main_screen.has_method("active_session_id_for_path"):
		var sid := String(_main_screen.active_session_id_for_path(path))
		if not sid.is_empty():
			return sid
	return path.get_file().get_basename()


func _find_node3d_viewport_container() -> Control:
	if _editor_interface == null:
		return null
	var base := _editor_interface.get_base_control()
	if base == null:
		return null
	var top: Node = base
	while top.get_parent() != null:
		top = top.get_parent()
	# The 3D editor lives under the editor MainScreen; scope out unrelated
	# viewports (e.g. the SceneImportSettingsDialog) by requiring a Node3DEditor ancestor.
	return _find_viewport_container_rec(top, false)


func _find_viewport_container_rec(node: Node, under_node3d_editor: bool) -> Control:
	var cls := node.get_class()
	var in_scope := under_node3d_editor or cls.contains("Node3DEditor")
	if in_scope and cls == "Node3DEditorViewportContainer" and node is Control:
		return node as Control
	for c in node.get_children():
		var found := _find_viewport_container_rec(c, in_scope)
		if found != null:
			return found
	return null
