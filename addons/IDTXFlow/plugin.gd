@tool
extends EditorPlugin

## IDTXFlow editor plugin.
## Adds a "Tools" menu entry to export the selected Node3D (with its children) to a USD file.
## The heavy lifting is done in the C++ GDExtension via `UsdExporter.export_node(...)`.

const MENU_ITEM_EXPORT := "Export selected node to USD…"
const _USD_EXTENSIONS := ["usda", "usdc", "usdz"]

var _file_dialog: EditorFileDialog = null
var _export_root: Node3D = null


func _enter_tree() -> void:
	add_tool_menu_item(MENU_ITEM_EXPORT, _on_export_pressed)


func _exit_tree() -> void:
	remove_tool_menu_item(MENU_ITEM_EXPORT)
	if _file_dialog != null:
		_file_dialog.queue_free()
		_file_dialog = null


func _get_plugin_name() -> String:
	return "IDTXFlow"


func _has_main_screen() -> bool:
	return false


## Resolve which node to export (selection first, else the edited scene root) and open the dialog.
func _on_export_pressed() -> void:
	var root: Node3D = null
	for node in get_editor_interface().get_selection().get_selected_nodes():
		if node is Node3D:
			root = node
			break
	if root == null:
		root = get_editor_interface().get_edited_scene_root() as Node3D
	if root == null:
		push_error("IDTXFlow export: select a Node3D (or open a 3D scene) first.")
		return
	_export_root = root
	_show_export_dialog()


## Lazily create and show the save-file dialog for choosing the output USD path/format.
func _show_export_dialog() -> void:
	if _file_dialog == null:
		_file_dialog = EditorFileDialog.new()
		_file_dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
		_file_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
		_file_dialog.add_filter("*.usda", "USD ASCII (text)")
		_file_dialog.add_filter("*.usdc", "USD Binary (compressed)")
		_file_dialog.add_filter("*.usdz", "USD Package")
		_file_dialog.file_selected.connect(_on_file_selected)
		get_editor_interface().get_base_control().add_child(_file_dialog)
	# Suggest a default filename derived from the export root's node name.
	var stem: String = _export_root.name if _export_root else "export"
	_file_dialog.current_file = stem + ".usda"
	_file_dialog.popup_centered_ratio(0.6)


## Run the export once the user confirmed an output path.
func _on_file_selected(path: String) -> void:
	# EditorFileDialog doesn't auto-append extensions; add one if missing.
	if path.get_extension().to_lower() not in _USD_EXTENSIONS:
		path += ".usda"
	var options := {
		"texture_dir": path.get_base_dir(),
		"up_axis_y": true,
	}
	var ok: bool = UsdExporter.export_node(_export_root, path, options)
	if ok:
		print("IDTXFlow: exported USD to ", path)
	else:
		push_error("IDTXFlow: USD export failed for " + path)
