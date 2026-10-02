@tool
extends EditorPlugin

const MainScreenScript := preload("res://addons/IDTXFlow/editor/import/import_manager.gd")
const UsdStageInspectorPlugin := preload("res://addons/IDTXFlow/editor/usd_stage_inspector_plugin.gd")
const SessionSceneIndicators := preload("res://addons/IDTXFlow/editor/session_scene/indicators.gd")
const SessionSceneCoordinator := preload("res://addons/IDTXFlow/editor/session_scene/coordinator.gd")
const SessionSaveCommitter := preload("res://addons/IDTXFlow/editor/session_scene/save_committer.gd")

var _main_screen: Control = null
var _inspector_plugin: EditorInspectorPlugin = null
var _indicators: RefCounted = null
var _session_scene_coordinator: RefCounted = null 
var _save_committer: RefCounted = null


func _enter_tree() -> void:
	# The native IdtxClient is created and registered as the "IdtxClient" engine
	# singleton from C++ at module init, and its poll() is driven by the frame
	# ticker — so no GDScript autoload is needed to host it.
	
	# Configure project settings used to persist IDTXFlow server urls and users
	_configure_import_project_settings()

	# Create the editor session-scene coordinator
	_session_scene_coordinator = SessionSceneCoordinator.new()
	_session_scene_coordinator.connect_client_signals()

	# Create the main screen and attach it to the editor's main viewport.
	_main_screen = MainScreenScript.new()
	_main_screen.name = "IDTXFlowMainScreen"
	if _main_screen.has_method("set_session_scene_coordinator"):
		_main_screen.set_session_scene_coordinator(_session_scene_coordinator)
	if _main_screen.has_method("set_editor_interface"):
		_main_screen.set_editor_interface(get_editor_interface())

	get_editor_interface().get_editor_main_screen().add_child(_main_screen)
	# Hidden by default; the editor shows it when the user selects this main screen.
	_make_visible(false)

	# Prune any stray transient session scenes left by a prior crash / hard close.
	if _session_scene_coordinator != null:
		_session_scene_coordinator.prune_stale_session_scenes()

	# A session-backed scene tab being closed is an implicit "leave session":
	# forward scene_closed to the session-scene coordinator so it can tear the session down and delete the transient file.
	scene_closed.connect(_on_scene_closed)

	# "Live session" visual indicators
	_indicators = SessionSceneIndicators.new()
	_indicators.setup(get_editor_interface(), _session_scene_coordinator)
	scene_changed.connect(_on_scene_changed_indicators)
	if _session_scene_coordinator != null and _session_scene_coordinator.has_signal("session_scenes_changed"):
		_session_scene_coordinator.session_scenes_changed.connect(_refresh_session_indicators)

	# Register the custom Inspector row (with "Connect" reload button) for UsdStageNode3D.stage_uri.
	_inspector_plugin = UsdStageInspectorPlugin.new()
	add_inspector_plugin(_inspector_plugin)

	# Commit-on-save orchestrator 
	_save_committer = SessionSaveCommitter.new()
	_save_committer.setup(get_editor_interface(), _session_scene_coordinator)

	_refresh_session_indicators()


func _on_scene_changed_indicators(_scene_root: Node) -> void:
	_refresh_session_indicators()


func _refresh_session_indicators() -> void:
	if _indicators != null:
		_indicators.refresh()


func _on_scene_closed(filepath: String) -> void:
	if _session_scene_coordinator != null:
		_session_scene_coordinator.on_session_scene_closed(filepath)


## Editor save hook: forward to the commit-on-save orchestrator.
func _save_external_data() -> void:
	if _save_committer != null:
		_save_committer.on_editor_save()


func _exit_tree() -> void:
	if scene_closed.is_connected(_on_scene_closed):
		scene_closed.disconnect(_on_scene_closed)
	if scene_changed.is_connected(_on_scene_changed_indicators):
		scene_changed.disconnect(_on_scene_changed_indicators)

	if _indicators != null:
		_indicators.clear()
		_indicators = null

	if _inspector_plugin != null:
		remove_inspector_plugin(_inspector_plugin)
		_inspector_plugin = null

	if _main_screen != null:
		_main_screen.queue_free()
		_main_screen = null

	if _save_committer != null:
		_save_committer.clear()
		_save_committer = null

	if _session_scene_coordinator != null:
		_session_scene_coordinator.disconnect_client_signals()
	_session_scene_coordinator = null

## Register the import persistence settings
##
## Layout:
##   idtxflow/import/servers     : Dictionary {url: {"users": [String], "last_user": String}}
##   idtxflow/import/last_server : String
func _configure_import_project_settings() -> void:
	# servers map 
	if not ProjectSettings.has_setting("idtxflow/import/servers"):
		ProjectSettings.set("idtxflow/import/servers", {})
	ProjectSettings.set_initial_value("idtxflow/import/servers", {})
	ProjectSettings.add_property_info({
		"name": "idtxflow/import/servers",
		"type": TYPE_DICTIONARY,
		"hint": PROPERTY_HINT_NONE,
	})

	# last used server
	if not ProjectSettings.has_setting("idtxflow/import/last_server"):
		ProjectSettings.set("idtxflow/import/last_server", "")
	ProjectSettings.set_initial_value("idtxflow/import/last_server", "")
	ProjectSettings.add_property_info({
		"name": "idtxflow/import/last_server",
		"type": TYPE_STRING,
		"hint": PROPERTY_HINT_NONE,
	})


func _get_plugin_name() -> String:
	return "IDTXFlow"


func _has_main_screen() -> bool:
	return true


func _make_visible(visible: bool) -> void:
	if _main_screen != null:
		_main_screen.visible = visible


func _get_plugin_icon() -> Texture2D:
	# Use a built-in editor icon as a placeholder.
	var theme: Theme = get_editor_interface().get_editor_theme()
	if theme != null and theme.has_icon("Node", "EditorIcons"):
		return theme.get_icon("Node", "EditorIcons")
	return null
