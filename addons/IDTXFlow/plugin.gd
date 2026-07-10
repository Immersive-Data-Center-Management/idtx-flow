@tool
extends EditorPlugin

const MainScreenScript := preload("res://addons/IDTXFlow/import_manager/import_manager.gd")

var _main_screen: Control = null


func _enter_tree() -> void:
	# Create the main screen and attach it to the editor's main viewport.
	_main_screen = MainScreenScript.new()
	_main_screen.name = "IDTXFlowMainScreen"
	if _main_screen.has_method("set_editor_interface"):
		_main_screen.set_editor_interface(get_editor_interface())

	get_editor_interface().get_editor_main_screen().add_child(_main_screen)
	# Hidden by default; the editor shows it when the user selects this main screen.
	_make_visible(false)


func _exit_tree() -> void:
	if _main_screen != null:
		_main_screen.queue_free()
		_main_screen = null


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
