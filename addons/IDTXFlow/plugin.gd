@tool
extends EditorPlugin

const MainScreenScript := preload("res://addons/IDTXFlow/import_manager/import_manager.gd")
const UsdStageInspectorPlugin := preload("res://addons/IDTXFlow/usd_stage_inspector_plugin.gd")

var _main_screen: Control = null
var _inspector_plugin: EditorInspectorPlugin = null


func _enter_tree() -> void:
	# The native IdtxClient is created and registered as the "IdtxClient" engine
	# singleton from C++ at module init, and its poll() is driven by the frame
	# ticker — so no GDScript autoload is needed to host it.
	
	# Configure project settings used to persist IDTXFlow server urls and users
	_configure_import_project_settings()

	# Create the main screen and attach it to the editor's main viewport.
	_main_screen = MainScreenScript.new()
	_main_screen.name = "IDTXFlowMainScreen"
	if _main_screen.has_method("set_editor_interface"):
		_main_screen.set_editor_interface(get_editor_interface())

	get_editor_interface().get_editor_main_screen().add_child(_main_screen)
	# Hidden by default; the editor shows it when the user selects this main screen.
	_make_visible(false)

	# Prune any stray transient session scenes left by a prior crash / hard close.
	if _main_screen.has_method("prune_stale_session_scenes"):
		_main_screen.prune_stale_session_scenes()

	# A session-backed scene tab being closed is an implicit "leave session":
	# forward scene_closed to the wizard so it can tear the session down and
	# delete the transient file.
	scene_closed.connect(_on_scene_closed)

	# Register the custom Inspector row (with "Connect" reload button) for
	# UsdStageNode3D.stage_uri.
	_inspector_plugin = UsdStageInspectorPlugin.new()
	add_inspector_plugin(_inspector_plugin)


func _on_scene_closed(filepath: String) -> void:
	if _main_screen != null and _main_screen.has_method("on_session_scene_closed"):
		_main_screen.on_session_scene_closed(filepath)


func _exit_tree() -> void:
	if scene_closed.is_connected(_on_scene_closed):
		scene_closed.disconnect(_on_scene_closed)

	if _inspector_plugin != null:
		remove_inspector_plugin(_inspector_plugin)
		_inspector_plugin = null

	if _main_screen != null:
		_main_screen.queue_free()
		_main_screen = null

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
