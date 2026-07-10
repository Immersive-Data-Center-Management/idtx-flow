@tool
extends Control

var file_dialog: FileDialog
var popup_window: Window
var current_step: int = 1
var selected_file_path: String = ""
var navigation_history: Array[String] = []
var history_index: int = -1
var is_grid_view: bool = false
var preview_panel: PanelContainer = null

func _ready():
	# File dialog for local filesystem
	file_dialog = FileDialog.new()
	file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	file_dialog.filters = ["*.usd", "*.usda", "*.usdc", "*.usdz"]
	file_dialog.file_selected.connect(_on_file_selected)
	add_child(file_dialog)
	
	# Create popup window 
	popup_window = Window.new()
	popup_window.title = "▢ USD Importer"
	popup_window.size = Vector2i(960, 600)
	popup_window.visible = false
	popup_window.close_requested.connect(_on_popup_close_requested)
	add_child(popup_window)

func _notification(what):
	if what == NOTIFICATION_VISIBILITY_CHANGED and visible:
		if popup_window:
			current_step = 1
			navigation_history.clear() 
			history_index = -1          
			_show_step(1)
			popup_window.visible = true
			popup_window.popup_centered()

func _on_popup_close_requested():
	if popup_window:
		popup_window.visible = false
		EditorInterface.set_main_screen_editor("3D")

func _on_upload_pressed():
	_show_step(2)

func _on_file_selected(path: String):
	selected_file_path = path
	print("File selected: ", path)
	
func _on_connect_pressed(uri: String):
	if uri.is_empty():
		push_warning("URI cannot be empty")
		return
	print("Connecting to URI: ", uri)
	# TODO: Set stage_uri on UsdStageNode3D
		
func _populate_file_list(folder_path: String, file_list: ItemList):
	file_list.clear()
	
	var dir = DirAccess.open(folder_path)
	if not dir:
		push_error("Unable to open directory: " + folder_path)
		return

	var folders: Array[String] = []
	var files: Array[String] = []

	dir.list_dir_begin()
	var item_name = dir.get_next()
	while item_name != "":
		# Skip hidden files/folders (starting with .)
		if not item_name.begins_with("."):
			if dir.current_is_dir():
				folders.append(item_name)
			else:
				var extension = item_name.get_extension().to_lower()
				if extension in ["usd", "usda", "usdc", "usdz"]:
					files.append(item_name)
		item_name = dir.get_next()
	dir.list_dir_end()
	
	# Sort alphabetically
	folders.sort()
	files.sort()
	
	# Add folders first (with folder icon/prefix)
	for folder_name in folders:
		file_list.add_item("📁 " + folder_name)
		var index = file_list.get_item_count() - 1
		file_list.set_item_metadata(index, {"type": "folder", "name": folder_name})
	
	# Then add USD files
	for file_name in files:
		file_list.add_item("📄 " + file_name)
		var index = file_list.get_item_count() - 1
		file_list.set_item_metadata(index, {"type": "file", "name": file_name})
		
func _on_file_item_selected(file_list: ItemList, index: int, folder_path: String):
	var metadata = file_list.get_item_metadata(index)
	
	if metadata["type"] == "folder":
		# Navigate into the folder
		var folder_name = metadata["name"]
		var new_path = folder_path.path_join(folder_name)
		
		# Get navigation buttons from file_list metadata
		var back_btn = file_list.get_meta("back_button") as Button
		var forward_btn = file_list.get_meta("forward_button") as Button
		var up_btn = file_list.get_meta("up_button") as Button
		var path_input = file_list.get_meta("path_input") as LineEdit
		
		if back_btn and forward_btn and up_btn and path_input:
			_navigate_to_path(new_path, path_input, file_list, back_btn, forward_btn, up_btn)
		
		# Disable Next button when navigating to folder
		var next_btn = file_list.get_meta("next_button") as Button
		if next_btn:
			next_btn.disabled = true
		
		# Hide preview panel
		_hide_preview_panel()
		
		# Clear selection
		selected_file_path = ""
		
	elif metadata["type"] == "file":
		# Select the file
		var file_name = metadata["name"]
		selected_file_path = folder_path.path_join(file_name)
		print("Selected file path: ", selected_file_path)
		
		# Enable Next button
		var next_btn = file_list.get_meta("next_button") as Button
		if next_btn:
			next_btn.disabled = false
		
		# Show/update preview panel
		var content_container = file_list.get_meta("content_container") as HBoxContainer
		if content_container:
			_update_preview_panel(selected_file_path, content_container)

func _on_step2_next():
	if selected_file_path.is_empty():
		push_warning("No file selected")
		return
	_show_step(3)
	
	
func _navigate_to_path(new_path: String, path_input: LineEdit, file_list: ItemList, 
					   back_btn: Button, forward_btn: Button, up_btn: Button):
	if new_path == path_input.text:
		return
	
	# Add to history if navigating forward (not back/forward navigation)
	if history_index == -1 or history_index == navigation_history.size() - 1:
		navigation_history.append(new_path)
		history_index = navigation_history.size() - 1
	else:
		# Clear forward history when navigating to a new path
		navigation_history.resize(history_index + 1)
		navigation_history.append(new_path)
		history_index = navigation_history.size() - 1
	
	path_input.text = new_path
	_populate_file_list(new_path, file_list)
	_update_navigation_buttons(back_btn, forward_btn, up_btn, new_path)

func _navigate_back(path_input: LineEdit, file_list: ItemList, 
					back_btn: Button, forward_btn: Button, up_btn: Button):
	if history_index > 0:
		history_index -= 1
		var new_path = navigation_history[history_index]
		path_input.text = new_path
		_populate_file_list(new_path, file_list)
		_update_navigation_buttons(back_btn, forward_btn, up_btn, new_path)

func _navigate_forward(path_input: LineEdit, file_list: ItemList,
					   back_btn: Button, forward_btn: Button, up_btn: Button):
	if history_index < navigation_history.size() - 1:
		history_index += 1
		var new_path = navigation_history[history_index]
		path_input.text = new_path
		_populate_file_list(new_path, file_list)
		_update_navigation_buttons(back_btn, forward_btn, up_btn, new_path)

func _navigate_up(path_input: LineEdit, file_list: ItemList,
				  back_btn: Button, forward_btn: Button, up_btn: Button):
	var current_path = path_input.text
	var parent_path = current_path.get_base_dir()
	
	# Avoid navigating beyond root
	if parent_path != current_path and not parent_path.is_empty():
		_navigate_to_path(parent_path, path_input, file_list, back_btn, forward_btn, up_btn)

func _update_navigation_buttons(back_btn: Button, forward_btn: Button, 
								up_btn: Button, current_path: String):
	back_btn.disabled = (history_index <= 0)
	forward_btn.disabled = (history_index >= navigation_history.size() - 1)
	
	# Disable up button if at root
	var parent_path = current_path.get_base_dir()
	up_btn.disabled = (parent_path == current_path or parent_path.is_empty())

func _toggle_view_mode(grid_btn: Button, list_btn: Button, file_list: ItemList):
	is_grid_view = !is_grid_view
	
	if is_grid_view:
		file_list.set_icon_mode(ItemList.ICON_MODE_TOP)
		file_list.set_fixed_column_width(80)
		file_list.set_max_columns(0)  # Auto-arrange
		grid_btn.flat = false  
		list_btn.flat = true   
	else:
		file_list.set_icon_mode(ItemList.ICON_MODE_LEFT)
		file_list.set_fixed_column_width(0)
		file_list.set_max_columns(1)
		grid_btn.flat = true  
		list_btn.flat = false  	
	
func _update_preview_panel(file_path: String, container: HBoxContainer):
	# Get or create preview panel
	if not preview_panel:
		preview_panel = PanelContainer.new()
		preview_panel.custom_minimum_size = Vector2(300, 0)
		preview_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
		
		var item_list_style = get_theme_stylebox("panel", "ItemList")
		if item_list_style:
			preview_panel.add_theme_stylebox_override("panel", item_list_style)
		
		# Create margin first
		var margin = MarginContainer.new()
		margin.add_theme_constant_override("margin_left", 10)
		margin.add_theme_constant_override("margin_top", 10)
		margin.add_theme_constant_override("margin_right", 10)
		margin.add_theme_constant_override("margin_bottom", 10)
		
		# Create vbox
		var vbox = VBoxContainer.new()
		vbox.add_theme_constant_override("separation", 8)
		
		# Correct hierarchy: PanelContainer -> MarginContainer -> VBoxContainer
		margin.add_child(vbox)
		preview_panel.add_child(margin)
		
		# Add to scene
		container.add_child(preview_panel)
	
	# Clear existing content
	var margin = preview_panel.get_child(0)
	if not margin:
		push_error("Preview panel margin not found")
		return
		
	var vbox = margin.get_child(0)
	if not vbox:
		push_error("Preview panel vbox not found")
		return
		
	for child in vbox.get_children():
		child.queue_free()
	
	# Add title
	var title = Label.new()
	title.text = "✅ Asset Details"
	title.add_theme_font_size_override("font_size", 14)
	vbox.add_child(title)
	
	var separator = HSeparator.new()
	separator.modulate = Color(1, 1, 1, 0.5)
	vbox.add_child(separator)
	
	# Load and display an img preview
	var image_path = "res://addons/IDTXFlow/usd_file_vec.png"
	var texture = load(image_path) as Texture2D

	if texture:
		var texture_rect = TextureRect.new()
		texture_rect.texture = texture
		texture_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		texture_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		texture_rect.custom_minimum_size = Vector2(225, 100)  
		vbox.add_child(texture_rect)
		
	# Get file info
	var file_name = file_path.get_file()
	var file_dir = file_path.get_base_dir()
	
	# File size
	var file_access = FileAccess.open(file_path, FileAccess.READ)
	var file_size = 0
	if file_access:
		file_size = file_access.get_length()
		file_access.close()
	
	var size_text = _format_file_size(file_size)
	
	# Last modified date
	var modified_time = FileAccess.get_modified_time(file_path)
	var date_text = Time.get_datetime_string_from_unix_time(modified_time, false).split("T")[0]
	
	# Add info labels
	_add_info_row(vbox, "File Name:", file_name)
	_add_info_row(vbox, "File Path:", file_dir, true)
	_add_info_row(vbox, "File Size:", size_text)
	_add_info_row(vbox, "Modified:", date_text)

func _add_info_row(parent: VBoxContainer, label_text: String, value_text: String, wrap: bool = false):
	var label = Label.new()
	label.text = label_text
	label.add_theme_font_size_override("font_size", 11)
	label.add_theme_color_override("font_color", Color(0.7, 0.7, 0.7))
	parent.add_child(label)
	
	var value = Label.new()
	value.text = value_text
	value.add_theme_font_size_override("font_size", 11)
	if wrap:
		value.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	parent.add_child(value)

func _format_file_size(bytes: int) -> String:
	if bytes < 1024:
		return str(bytes) + " B"
	elif bytes < 1024 * 1024:
		return "%.2f KB" % (bytes / 1024.0)
	elif bytes < 1024 * 1024 * 1024:
		return "%.2f MB" % (bytes / (1024.0 * 1024.0))
	else:
		return "%.2f GB" % (bytes / (1024.0 * 1024.0 * 1024.0))

func _hide_preview_panel():
	if preview_panel and preview_panel.is_inside_tree():
		preview_panel.queue_free()
		preview_panel = null
		
func _on_import_confirmed():
	print("Importing USD file: ", selected_file_path)
	var root = EditorInterface.get_edited_scene_root()
	if root == null:
		push_error("No edited scene")
		return
	var node = ClassDB.instantiate("UsdStageNode3D")
	if node == null:
		push_error("Unable to instantiate UsdStageNode3D")
		return
	node.name = "UsdStageNode3D"
	root.add_child(node)
	node.owner = root
	node.set_stage_uri(selected_file_path)
	
	print("UsdStageNode3D added to scene")
	_on_popup_close_requested()
	
	# Switch to the 3D view after import
	EditorInterface.set_main_screen_editor("3D")
	
func _show_step(step: int):
	current_step = step
	
	# Hide preview panel when changing steps
	_hide_preview_panel()  
	
	# Clear previous content
	for child in popup_window.get_children():
		child.queue_free()
	
	if step == 1:
		_build_step1()
	elif step == 2:
		_build_step2()
	elif step == 3:
		_build_step3()

func _build_step1():
	# Main container with margin
	var margin = MarginContainer.new()
	var bg_panel = Panel.new()
	bg_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var bg_style = get_theme_stylebox("panel", "Panel")
	if bg_style:
		bg_panel.add_theme_stylebox_override("panel", bg_style)
	popup_window.add_child(bg_panel)
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 20)
	margin.add_theme_constant_override("margin_top", 20)
	margin.add_theme_constant_override("margin_right", 20)
	margin.add_theme_constant_override("margin_bottom", 20)
	popup_window.add_child(margin)
	
	# Window content - centered container
	var vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	margin.add_child(vbox)
	
	# Step description
	var step_label = Label.new()
	step_label.text = "Step 1 of 3 ― Select importer: Connect to the USD asset server or import from local files"
	step_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(step_label)
	
	# Separator line
	var separator = HSeparator.new()
	separator.modulate = Color(1, 1, 1, 0.5)
	vbox.add_child(separator)
	
	# Small top spacer
	var top_spacer = Control.new()
	top_spacer.custom_minimum_size = Vector2(0, 20)
	vbox.add_child(top_spacer)

	# URI input and Connect button (centered)
	var uri_container = CenterContainer.new()
	vbox.add_child(uri_container)

	var hbox = HBoxContainer.new()
	hbox.add_theme_constant_override("separation", 10)
	uri_container.add_child(hbox)

	var uri_input = LineEdit.new()
	uri_input.placeholder_text = "https://aetherra-prime.aas-showroom.msp02.shoot.gardener.cc-one.showroom.apeirora.eu/"
	uri_input.custom_minimum_size = Vector2(400, 0)
	hbox.add_child(uri_input)

	var connect_btn = Button.new()
	connect_btn.text = "Connect"
	connect_btn.pressed.connect(func(): _on_connect_pressed(uri_input.text))
	hbox.add_child(connect_btn)

	# "or" label (centered)
	var or_container = CenterContainer.new()
	vbox.add_child(or_container)

	var or_label = Label.new()
	or_label.text = "or"
	or_container.add_child(or_label)
	
	# Centered button container
	var button_container = CenterContainer.new()
	vbox.add_child(button_container)
	
	var upload_btn = Button.new()
	upload_btn.text = "📄 Import USD from local files"
	upload_btn.pressed.connect(_on_upload_pressed)
	button_container.add_child(upload_btn)
	
	# Spacer to push separator to bottom
	var bottom_spacer = Control.new()
	bottom_spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(bottom_spacer)
	
	# Bottom separator line
	var separator_bottom = HSeparator.new()
	separator_bottom.modulate = Color(1, 1, 1, 0.5)
	vbox.add_child(separator_bottom)

	# Cancel button (bottom-right)
	var cancel_container = HBoxContainer.new()
	cancel_container.alignment = BoxContainer.ALIGNMENT_END
	vbox.add_child(cancel_container)

	var cancel_btn = Button.new()
	cancel_btn.text = "Cancel"
	cancel_btn.pressed.connect(_on_popup_close_requested)
	cancel_container.add_child(cancel_btn)
	
func _build_step2():
	var margin = MarginContainer.new()
	var bg_panel = Panel.new()
	bg_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var bg_style = get_theme_stylebox("panel", "Panel")
	if bg_style:
		bg_panel.add_theme_stylebox_override("panel", bg_style)
	popup_window.add_child(bg_panel)
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 20)
	margin.add_theme_constant_override("margin_top", 20)
	margin.add_theme_constant_override("margin_right", 20)
	margin.add_theme_constant_override("margin_bottom", 20)
	popup_window.add_child(margin)
	
	var vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	margin.add_child(vbox)
	
	var step_label = Label.new()
	step_label.text = "Step 2 of 3 ― Browse: Select file to import"
	step_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(step_label)
	
	var separator = HSeparator.new()
	separator.modulate = Color(1, 1, 1, 0.5)
	vbox.add_child(separator)
	
	# Top spacer
	var top_spacer = Control.new()
	top_spacer.custom_minimum_size = Vector2(0, 20)
	vbox.add_child(top_spacer)

	# Path selection row with navigation buttons
	var path_row = HBoxContainer.new()
	path_row.add_theme_constant_override("separation", 5)
	vbox.add_child(path_row)

	var path_label = Label.new()
	path_label.text = "Path:"
	path_label.custom_minimum_size = Vector2(50, 0)
	path_row.add_child(path_label)

	var path_input = LineEdit.new()
	path_input.editable = false
	path_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	var project_path = ProjectSettings.globalize_path("res://")
	path_input.text = project_path
	path_row.add_child(path_input)
	
	# Initialize navigation history
	navigation_history.clear()
	navigation_history.append(project_path)
	history_index = 0

	# Navigation buttons container
	var nav_buttons = HBoxContainer.new()
	nav_buttons.add_theme_constant_override("separation", 2)
	path_row.add_child(nav_buttons)

	# 1. Back button
	var back_btn = Button.new()
	back_btn.text = "<"
	back_btn.custom_minimum_size = Vector2(30, 0)
	back_btn.disabled = true
	back_btn.tooltip_text = "Back"
	nav_buttons.add_child(back_btn)

	# 2. Forward button
	var forward_btn = Button.new()
	forward_btn.text = ">"
	forward_btn.custom_minimum_size = Vector2(30, 0)
	forward_btn.disabled = true
	forward_btn.tooltip_text = "Forward"
	nav_buttons.add_child(forward_btn)

	# 3. Up/Parent button
	var up_btn = Button.new()
	up_btn.text = "∧"
	up_btn.custom_minimum_size = Vector2(30, 0)
	up_btn.tooltip_text = "Parent Folder"
	nav_buttons.add_child(up_btn)

	# Separator
	var view_separator = VSeparator.new()
	view_separator.modulate = Color(1, 1, 1, 0.5)
	nav_buttons.add_child(view_separator)

	# 4. Grid view button
	var grid_btn = Button.new()
	grid_btn.text = "⊞"
	grid_btn.custom_minimum_size = Vector2(30, 0)
	grid_btn.tooltip_text = "Grid View"
	nav_buttons.add_child(grid_btn)

	# 5. List view button
	var list_btn = Button.new()
	list_btn.text = "☰"
	list_btn.custom_minimum_size = Vector2(30, 0)
	list_btn.tooltip_text = "List View"
	grid_btn.flat = true
	nav_buttons.add_child(list_btn)

	# Content area with file list and preview panel (side by side)
	var content_container = HBoxContainer.new()
	content_container.add_theme_constant_override("separation", 10)
	content_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(content_container)

	# Left side: File list container
	var file_list_container = VBoxContainer.new()
	file_list_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	file_list_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	content_container.add_child(file_list_container)

	var scroll_container = ScrollContainer.new()
	scroll_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll_container.custom_minimum_size = Vector2(0, 200)
	file_list_container.add_child(scroll_container)

	var file_list = ItemList.new()
	file_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	file_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	file_list.item_selected.connect(func(index): _on_file_item_selected(file_list, index, path_input.text))
	scroll_container.add_child(file_list)

	# Save references for navigation within file list
	file_list.set_meta("path_input", path_input)
	file_list.set_meta("back_button", back_btn)
	file_list.set_meta("forward_button", forward_btn)
	file_list.set_meta("up_button", up_btn)
	file_list.set_meta("content_container", content_container)
	
	# Populate initial file list
	_populate_file_list(project_path, file_list)
	
	# Update navigation buttons state
	_update_navigation_buttons(back_btn, forward_btn, up_btn, project_path)

	# Connect navigation button signals
	back_btn.pressed.connect(func(): _navigate_back(path_input, file_list, back_btn, forward_btn, up_btn))
	forward_btn.pressed.connect(func(): _navigate_forward(path_input, file_list, back_btn, forward_btn, up_btn))
	up_btn.pressed.connect(func(): _navigate_up(path_input, file_list, back_btn, forward_btn, up_btn))
	
	# Connect view toggle buttons
	grid_btn.pressed.connect(func(): _toggle_view_mode(grid_btn, list_btn, file_list))
	list_btn.pressed.connect(func(): _toggle_view_mode(grid_btn, list_btn, file_list))

	# Bottom separator
	var separator_bottom = HSeparator.new()
	separator_bottom.modulate = Color(1, 1, 1, 0.5)
	vbox.add_child(separator_bottom)

	# Bottom buttons
	var bottom_buttons = HBoxContainer.new()
	vbox.add_child(bottom_buttons)

	var back_to_step1_btn = Button.new()
	back_to_step1_btn.text = "Back"
	back_to_step1_btn.pressed.connect(func(): _show_step(1))
	bottom_buttons.add_child(back_to_step1_btn)
	
	var spacer = Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bottom_buttons.add_child(spacer)
	
	var cancel_btn = Button.new()
	cancel_btn.text = "Cancel"
	cancel_btn.pressed.connect(_on_popup_close_requested)
	bottom_buttons.add_child(cancel_btn)
	
	var next_btn = Button.new()
	next_btn.text = "Next"
	next_btn.disabled = true  
	next_btn.pressed.connect(func(): _on_step2_next())
	bottom_buttons.add_child(next_btn)
	
	# Save reference to Next Button in order to enable it when a file is selected
	file_list.set_meta("next_button", next_btn)
	
func _build_step3():
	var margin = MarginContainer.new()
	var bg_panel = Panel.new()
	bg_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var bg_style = get_theme_stylebox("panel", "Panel")
	if bg_style:
		bg_panel.add_theme_stylebox_override("panel", bg_style)
	popup_window.add_child(bg_panel)
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 20)
	margin.add_theme_constant_override("margin_top", 20)
	margin.add_theme_constant_override("margin_right", 20)
	margin.add_theme_constant_override("margin_bottom", 20)
	popup_window.add_child(margin)
	
	var vbox = VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	margin.add_child(vbox)
	
	var step_label = Label.new()
	step_label.text = "Step 3 of 3 ― Configure: Define import settings"
	step_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	vbox.add_child(step_label)
	
	var separator = HSeparator.new()
	separator.modulate = Color(1, 1, 1, 0.5)
	vbox.add_child(separator)
	
	var top_spacer = Control.new()
	top_spacer.custom_minimum_size = Vector2(0, 20)
	vbox.add_child(top_spacer)
	
	# Main content area (HBoxContainer for left/right layout)
	var content_hbox = HBoxContainer.new()
	content_hbox.add_theme_constant_override("separation", 20)
	content_hbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(content_hbox)

	# LEFT SIDE: Prim Types (collapsible)
	var prim_types_container = VBoxContainer.new()
	prim_types_container.add_theme_constant_override("separation", 5)
	prim_types_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	content_hbox.add_child(prim_types_container)

	var prim_types_btn = Button.new()
	prim_types_btn.text = "▼ Prim Types"
	prim_types_btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
	prim_types_btn.add_theme_font_size_override("font_size", 14)
	prim_types_container.add_child(prim_types_btn)

	var prim_types_content = HBoxContainer.new()
	prim_types_content.add_theme_constant_override("separation", 5)
	prim_types_container.add_child(prim_types_content)
	
	var include_label = Label.new()
	include_label.text = "Include:"
	include_label.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	prim_types_content.add_child(include_label)
	
	var checkboxes_vbox = VBoxContainer.new()
	checkboxes_vbox.add_theme_constant_override("separation", .5)
	prim_types_content.add_child(checkboxes_vbox)

	for letter in ["A", "B", "C", "D"]:
		var checkbox = CheckBox.new()
		checkbox.text = letter
		checkbox.flat = true
		checkboxes_vbox.add_child(checkbox)

	prim_types_btn.pressed.connect(func():
		prim_types_content.visible = !prim_types_content.visible
		prim_types_btn.text = "▼ Prim Types" if prim_types_content.visible else "▶ Prim Types"
	)

	# RIGHT SIDE: Import Definition + Import Settings
	var right_vbox = VBoxContainer.new()
	right_vbox.add_theme_constant_override("separation", 5)
	right_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	content_hbox.add_child(right_vbox)

	# Import Definition section (collapsible)
	var import_def_container = VBoxContainer.new()
	import_def_container.add_theme_constant_override("separation", 5)
	right_vbox.add_child(import_def_container)

	var import_def_btn = Button.new()
	import_def_btn.text = "▼ Import Definition"
	import_def_btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
	import_def_btn.add_theme_font_size_override("font_size", 14)
	import_def_container.add_child(import_def_btn)

	var import_def_content = VBoxContainer.new()
	import_def_content.add_theme_constant_override("separation", 5)
	import_def_container.add_child(import_def_content)

	var lights_check = CheckBox.new()
	lights_check.text = "Lights"
	lights_check.flat = true
	import_def_content.add_child(lights_check)

	var cameras_check = CheckBox.new()
	cameras_check.text = "Cameras"
	cameras_check.flat = true
	import_def_content.add_child(cameras_check)

	import_def_btn.pressed.connect(func():
		import_def_content.visible = !import_def_content.visible
		import_def_btn.text = "▼ Import Definition" if import_def_content.visible else "▶ Import Definition"
	)

	# Import Settings section (collapsible)
	var import_settings_container = VBoxContainer.new()
	import_settings_container.add_theme_constant_override("separation", 20)
	right_vbox.add_child(import_settings_container)

	var import_settings_btn = Button.new()
	import_settings_btn.text = "▼ Import Settings"
	import_settings_btn.alignment = HORIZONTAL_ALIGNMENT_LEFT
	import_settings_btn.add_theme_font_size_override("font_size", 14)
	import_settings_container.add_child(import_settings_btn)

	var import_settings_content = VBoxContainer.new()
	import_settings_content.add_theme_constant_override("separation", 10)
	import_settings_container.add_child(import_settings_content)

	# Scale row
	var scale_row = HBoxContainer.new()
	import_settings_content.add_child(scale_row)

	var scale_label = Label.new()
	scale_label.text = "Scale:"
	scale_label.custom_minimum_size = Vector2(120, 0)
	scale_row.add_child(scale_label)

	var scale_input = SpinBox.new()
	scale_input.min_value = 0.001
	scale_input.max_value = 1000.0
	scale_input.step = 1.0
	scale_input.value = 1.0
	scale_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scale_row.add_child(scale_input)

	# Light Intensity row
	var intensity_row = HBoxContainer.new()
	import_settings_content.add_child(intensity_row)

	var intensity_label = Label.new()
	intensity_label.text = "Light Intensity:"
	intensity_label.custom_minimum_size = Vector2(120, 0)
	intensity_row.add_child(intensity_label)

	var intensity_input = SpinBox.new()
	intensity_input.min_value = 0.0
	intensity_input.max_value = 100.0
	intensity_input.step = 1.0
	intensity_input.value = 1.0
	intensity_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	intensity_row.add_child(intensity_input)

	import_settings_btn.pressed.connect(func():
		import_settings_content.visible = !import_settings_content.visible
		import_settings_btn.text = "▼ Import Settings" if import_settings_content.visible else "▶ Import Settings"
	)
	
	# Bottom spacer
	var bottom_spacer = Control.new()
	bottom_spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(bottom_spacer)
	
	var separator_bottom = HSeparator.new()
	separator_bottom.modulate = Color(1, 1, 1, 0.5)
	vbox.add_child(separator_bottom)
	
	# Bottom buttons
	var bottom_buttons = HBoxContainer.new()
	vbox.add_child(bottom_buttons)
	
	var back_btn = Button.new()
	back_btn.text = "Back"
	back_btn.pressed.connect(func(): _show_step(2))
	bottom_buttons.add_child(back_btn)
	
	var spacer = Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bottom_buttons.add_child(spacer)
	
	var cancel_btn = Button.new()
	cancel_btn.text = "Cancel"
	cancel_btn.pressed.connect(_on_popup_close_requested)
	bottom_buttons.add_child(cancel_btn)
	
	var import_btn = Button.new()
	import_btn.text = "Import"
	import_btn.pressed.connect(func(): _on_import_confirmed())
	bottom_buttons.add_child(import_btn)