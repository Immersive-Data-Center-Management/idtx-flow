@tool
extends EditorPlugin

#var inspector_plugin: UsdNode3DInspectorPlugin


func _enter_tree() -> void:
	pass
#	if inspector_plugin == null:
#		inspector_plugin = UsdNode3DInspectorPlugin.new()	
#		add_inspector_plugin(inspector_plugin)

func _exit_tree() -> void:
	pass
#	if inspector_plugin != null:
#		remove_inspector_plugin(inspector_plugin)
#		inspector_plugin = null
	
func _get_plugin_name() -> String:
	return "IDTXFlow"

func _has_main_screen() -> bool:
	return false
