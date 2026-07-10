@tool
extends EditorPlugin

var inspector_plugin
var usd_importer_panel 

func _enter_tree() -> void:
	if inspector_plugin == null:
		inspector_plugin = preload("usd_inspector_plugin.gd").new()	
		add_inspector_plugin(inspector_plugin)
		
	usd_importer_panel = preload("usd_importer_panel.gd").new()
	get_editor_interface().get_editor_main_screen().add_child(usd_importer_panel)
	usd_importer_panel.hide()

func _exit_tree() -> void:
	if inspector_plugin != null:
		remove_inspector_plugin(inspector_plugin)
		inspector_plugin = null
		
	if usd_importer_panel != null:
		if usd_importer_panel.get_parent():
			usd_importer_panel.get_parent().remove_child(usd_importer_panel)
		usd_importer_panel.queue_free()
		usd_importer_panel = null
	
func _get_plugin_name() -> String:
	return "▢ USD Importer"

func _has_main_screen() -> bool:
	return true

func _make_visible(visible: bool) -> void:
	if usd_importer_panel:
		usd_importer_panel.visible = visible