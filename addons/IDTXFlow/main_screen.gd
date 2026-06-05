@tool
extends Control

## Main screen for the IDTXFlow plugin.
##
## Provides a file browser limited to USD related files (*.usd, *.usda, *.usdc, *.usdz)
## inside the project's res:// folder, plus an "Import" action button which becomes
## active only after a USD file has been selected.

const USD_EXTENSIONS := ["usd", "usda", "usdc", "usdz"]

var _editor_interface: EditorInterface
var _file_tree: Tree
var _path_label: Label
var _import_button: Button
var _selected_file_path: String = ""

# Last node that was selected in the scene tree dock when Import was triggered.
var _last_imported_target_node: Node = null


func set_editor_interface(editor_interface: EditorInterface) -> void:
	_editor_interface = editor_interface


func _ready() -> void:
	_build_ui()
	_populate_tree()


func _build_ui() -> void:
	# Make this Control fill the entire main-screen viewport.
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL

	var root_vbox := VBoxContainer.new()
	root_vbox.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root_vbox.offset_left = 8
	root_vbox.offset_top = 8
	root_vbox.offset_right = -8
	root_vbox.offset_bottom = -8
	root_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root_vbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(root_vbox)

	var title := Label.new()
	title.text = "IDTXFlow - USD File Browser"
	title.add_theme_font_size_override("font_size", 16)
	root_vbox.add_child(title)

	var description := Label.new()
	description.text = "Browse the project for USD files (*.usd, *.usda, *.usdc, *.usdz) and import them."
	description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	root_vbox.add_child(description)

	var toolbar := HBoxContainer.new()
	root_vbox.add_child(toolbar)

	var refresh_button := Button.new()
	refresh_button.text = "Refresh"
	refresh_button.pressed.connect(_on_refresh_pressed)
	toolbar.add_child(refresh_button)

	_path_label = Label.new()
	_path_label.text = "No file selected"
	_path_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_path_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	toolbar.add_child(_path_label)

	# Tree (file browser)
	_file_tree = Tree.new()
	_file_tree.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_file_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_file_tree.hide_root = false
	_file_tree.select_mode = Tree.SELECT_SINGLE
	_file_tree.item_selected.connect(_on_item_selected)
	_file_tree.item_activated.connect(_on_item_activated)
	root_vbox.add_child(_file_tree)

	# Bottom bar with Import button on the right
	var bottom_bar := HBoxContainer.new()
	bottom_bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root_vbox.add_child(bottom_bar)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bottom_bar.add_child(spacer)

	_import_button = Button.new()
	_import_button.text = "Import"
	_import_button.disabled = true
	_import_button.pressed.connect(_on_import_pressed)
	bottom_bar.add_child(_import_button)


func _on_refresh_pressed() -> void:
	_populate_tree()


func _populate_tree() -> void:
	_file_tree.clear()
	_selected_file_path = ""
	_path_label.text = "No file selected"
	_import_button.disabled = true

	var root := _file_tree.create_item()
	root.set_text(0, "res://")
	root.set_metadata(0, "res://")
	root.set_selectable(0, false)

	_add_directory_recursive("res://", root)

	# Expand the root for convenience.
	root.set_collapsed(false)


# Returns true when the directory (or any of its subdirectories) contained
# at least one USD file. This enables us to skip showing empty branches.
func _add_directory_recursive(path: String, parent_item: TreeItem) -> bool:
	var dir := DirAccess.open(path)
	if dir == null:
		return false

	dir.include_navigational = false
	dir.include_hidden = false

	var directories: PackedStringArray = []
	var files: PackedStringArray = []

	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if dir.current_is_dir():
			directories.append(entry)
		else:
			files.append(entry)
		entry = dir.get_next()
	dir.list_dir_end()

	directories.sort()
	files.sort()

	var has_any_usd := false

	# First pass: process subdirectories (recursive)
	for d in directories:
		# Skip the addon's own bin and hidden Godot folders to limit noise
		if d.begins_with("."):
			continue
		var sub_path := path
		if not sub_path.ends_with("/"):
			sub_path += "/"
		sub_path += d

		var dir_item := _file_tree.create_item(parent_item)
		dir_item.set_text(0, d + "/")
		dir_item.set_metadata(0, sub_path)
		dir_item.set_selectable(0, false)
		dir_item.set_collapsed(true)

		var subtree_has_usd := _add_directory_recursive(sub_path, dir_item)
		if not subtree_has_usd:
			# Remove empty directory branches to keep the browser focused on USD assets
			parent_item.remove_child(dir_item)
		else:
			has_any_usd = true

	# Second pass: process files in this directory
	for f in files:
		var ext := f.get_extension().to_lower()
		if not USD_EXTENSIONS.has(ext):
			continue
		var file_path := path
		if not file_path.ends_with("/"):
			file_path += "/"
		file_path += f

		var file_item := _file_tree.create_item(parent_item)
		file_item.set_text(0, f)
		file_item.set_metadata(0, file_path)
		file_item.set_selectable(0, true)
		has_any_usd = true

	return has_any_usd


func _on_item_selected() -> void:
	var item := _file_tree.get_selected()
	if item == null:
		_selected_file_path = ""
		_import_button.disabled = true
		_path_label.text = "No file selected"
		return

	var meta = item.get_metadata(0)
	if typeof(meta) != TYPE_STRING:
		_selected_file_path = ""
		_import_button.disabled = true
		_path_label.text = "No file selected"
		return

	var path: String = meta
	# Only treat USD files as valid selections.
	var ext := path.get_extension().to_lower()
	if USD_EXTENSIONS.has(ext):
		_selected_file_path = path
		_path_label.text = "Selected: " + path
		_import_button.disabled = false
	else:
		_selected_file_path = ""
		_path_label.text = "No file selected"
		_import_button.disabled = true


func _on_item_activated() -> void:
	# Double-click on a file behaves like the Import button.
	if not _import_button.disabled:
		_on_import_pressed()


# ---------------------------------------------------------------------------
# Import handler (stub)
# ---------------------------------------------------------------------------

func _on_import_pressed() -> void:
	_handle_import(_selected_file_path)


# Stub handler. For now it only retrieves the currently selected scene tree
# node and stores it for later processing, plus prints debug output.
func _handle_import(file_path: String) -> void:
	if file_path.is_empty():
		print("[IDTXFlow] Import requested but no file is selected.")
		return

	var selected_node: Node = _get_currently_selected_scene_node()
	_last_imported_target_node = selected_node

	print("[IDTXFlow] Import requested.")
	print("  File: ", file_path)
	if selected_node != null:
		print("  Target scene node: ", selected_node.name, " (", selected_node.get_class(), ")")
		print("  Target node path: ", selected_node.get_path())
		# create an UsdStageNode3D as child of the selected node and set the stage uri to the selected path to trigger
		# the import
		var stage_node: UsdStageNode3D = UsdStageNode3D.new()
		if _editor_interface:
			stage_node.owner = _editor_interface.get_edited_scene_root()
		else:
			stage_node.owner = selected_node
		# set the uri will trigger the import
		stage_node.stage_uri = file_path
	else:
		print("  Target scene node: <none selected>")


func _get_currently_selected_scene_node() -> Node:
	if _editor_interface == null:
		return null

	# Prefer the user's explicit selection in the Scene dock.
	var selection := _editor_interface.get_selection()
	if selection != null:
		var nodes := selection.get_selected_nodes()
		if not nodes.is_empty():
			return nodes[0]

	# Fall back to the root of the currently edited scene tree.
	var edited_scene_root := _editor_interface.get_edited_scene_root()
	if edited_scene_root != null:
		return edited_scene_root

	return null
