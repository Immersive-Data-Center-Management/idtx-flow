@tool
extends EditorScript

## Open the standalone primitive test window for the editor's current live session.

const ToolWindow = preload("prim_tools_window.gd")
const WINDOW_TAG := "idtxflow_standalone_prim_tools"


func _run() -> void:
	open_window(get_editor_interface())


## Return the existing test window, or attach one beneath the editor UI.
static func open_window(editor: Object) -> Window:
	if not is_instance_valid(editor) or not editor.has_method("get_base_control"):
		return null
	var base: Control = editor.get_base_control()
	if not is_instance_valid(base):
		return null
	for child in base.get_children():
		if child is Window and child.has_meta(WINDOW_TAG) and not child.is_queued_for_deletion():
			child.show()
			child.grab_focus()
			return child
	var plugin := find_plugin(base.get_tree().root)
	var client: Object = Engine.get_singleton("IdtxClient") if Engine.has_singleton("IdtxClient") else null
	var window := ToolWindow.new()
	window.set_meta(WINDOW_TAG, true)
	base.add_child(window)
	window.setup(editor, plugin, client)
	window.popup_centered()
	return window


## Locate the committed addon's plugin without loading or changing its scripts.
static func find_plugin(root: Node) -> Object:
	if not is_instance_valid(root):
		return null
	var script: Script = root.get_script()
	if script != null and script.resource_path == "res://addons/IDTXFlow/plugin.gd":
		return root
	for child in root.get_children():
		var plugin := find_plugin(child)
		if plugin != null:
			return plugin
	return null
