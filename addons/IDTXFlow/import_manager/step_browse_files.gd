@tool
extends VBoxContainer

## Step 2 - Browse res:// for USD files.

signal file_selected(path: String)
signal back_requested
signal cancel_requested
signal next_requested

const WizardTheme  := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const AssetPanel   := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")

const USD_EXTENSIONS := ["usd", "usda", "usdc", "usdz"]
const ROOT_PATH      := "res://"

var _current_path: String = ROOT_PATH
var _selected_file: String = ""
var _item_meta: Array = []

var _path_display: LineEdit
var _item_list: ItemList
var _detail_panel: Node
var _detail_container: Control
var _footer: Node


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()
	_populate_list()


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(2, 3, "Browse:", "Select file to import")

	_build_path_bar()
	_build_split()
	_build_footer()


func _build_path_bar() -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", WizardTheme.px(6))
	add_child(row)

	var cap := Label.new()
	cap.text = "Path:"
	cap.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT_CAPTION)
	cap.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_CAPTION))
	row.add_child(cap)

	_path_display = LineEdit.new()
	_path_display.editable = false
	_path_display.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_path_display.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	WizardTheme.apply_line_edit_style(_path_display)
	row.add_child(_path_display)

	var back_nav := _make_nav_button("<")
	back_nav.disabled = true
	row.add_child(back_nav)

	var fwd_nav := _make_nav_button(">")
	fwd_nav.disabled = true
	row.add_child(fwd_nav)

	var up_nav := _make_nav_button("^")
	up_nav.pressed.connect(_on_up_pressed)
	row.add_child(up_nav)


func _build_split() -> void:
	var split := HSplitContainer.new()
	split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(split)

	# Left: list panel
	var list_panel := PanelContainer.new()
	list_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	list_panel.add_theme_stylebox_override("panel", WizardTheme.make_panel_style(WizardTheme.COLOR_PANEL))
	split.add_child(list_panel)

	_item_list = ItemList.new()
	_item_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_item_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_item_list.select_mode = ItemList.SELECT_SINGLE
	_item_list.item_selected.connect(_on_item_selected)
	_item_list.item_activated.connect(_on_item_activated)

	var list_bg := WizardTheme.make_flat_style(WizardTheme.COLOR_PANEL, 0)
	_item_list.add_theme_stylebox_override("panel", list_bg)
	var sel_style := WizardTheme.make_flat_style(WizardTheme.COLOR_SELECTION, 3)
	_item_list.add_theme_stylebox_override("selected", sel_style)
	_item_list.add_theme_stylebox_override("selected_focus", sel_style)
	_item_list.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	_item_list.add_theme_color_override("font_selected_color", Color.WHITE)
	_item_list.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	_item_list.add_theme_constant_override("v_separation", WizardTheme.px(6))
	_item_list.add_theme_constant_override("h_separation", WizardTheme.px(6))
	_item_list.fixed_icon_size = Vector2i(WizardTheme.px(16), WizardTheme.px(16))
	list_panel.add_child(_item_list)

	# Right: detail container
	_detail_container = PanelContainer.new()
	_detail_container.custom_minimum_size = Vector2(WizardTheme.px(260), 0)
	_detail_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_detail_container.visible = false
	_detail_container.add_theme_stylebox_override("panel", WizardTheme.make_flat_style(WizardTheme.COLOR_BG, 0))
	split.add_child(_detail_container)

	_detail_panel = AssetPanel.new()
	_detail_container.add_child(_detail_panel)


func _build_footer() -> void:
	_footer = WizardFooter.new()
	add_child(_footer)
	_footer.setup(true, "Next", false)
	_footer.back_pressed.connect(func(): back_requested.emit())
	_footer.cancel_pressed.connect(func(): cancel_requested.emit())
	_footer.primary_pressed.connect(_on_next_pressed)


func _make_nav_button(text: String) -> Button:
	var b := Button.new()
	b.text = text
	var s := WizardTheme.px(WizardTheme.NAV_BTN_SIZE)
	b.custom_minimum_size = Vector2(s, s)
	WizardTheme.apply_secondary_button(b)
	return b


func _populate_list() -> void:
	_item_list.clear()
	_item_meta.clear()
	_selected_file = ""
	_detail_container.visible = false
	_footer.set_primary_enabled(false)
	_path_display.text = _current_path

	var dir := DirAccess.open(_current_path)
	if dir == null:
		return
	dir.include_navigational = false
	dir.include_hidden = false

	var dirs: PackedStringArray = []
	var files: PackedStringArray = []

	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if not entry.begins_with("."):
			if dir.current_is_dir():
				dirs.append(entry)
			else:
				var ext := entry.get_extension().to_lower()
				if ext in USD_EXTENSIONS:
					files.append(entry)
		entry = dir.get_next()
	dir.list_dir_end()

	dirs.sort()
	files.sort()

	var folder_icon := WizardTheme.get_editor_icon(self, "Folder")
	var file_icon := WizardTheme.get_editor_icon(self, "PackedScene", "ResourcePreloader")

	for d in dirs:
		var full := _join(_current_path, d)
		var idx := _item_list.add_item(d, folder_icon)
		_item_list.set_item_metadata(idx, {"path": full, "is_dir": true})
		_item_meta.append({"path": full, "is_dir": true})

	for f in files:
		var full := _join(_current_path, f)
		var idx := _item_list.add_item(f, file_icon)
		_item_list.set_item_metadata(idx, {"path": full, "is_dir": false})
		_item_meta.append({"path": full, "is_dir": false})


func _join(base: String, name: String) -> String:
	if base.ends_with("/"):
		return base + name
	return base + "/" + name


func _on_item_selected(index: int) -> void:
	var meta = _item_list.get_item_metadata(index)
	if typeof(meta) != TYPE_DICTIONARY:
		return
	if meta.get("is_dir", false):
		# Selecting a folder does not enable Next
		_selected_file = ""
		_detail_container.visible = false
		_footer.set_primary_enabled(false)
		return

	_selected_file = meta["path"]
	_detail_container.visible = true
	if _detail_panel and _detail_panel.has_method("set_header_style"):
		_detail_panel.set_header_style(0)  # ASSET_DETAILS
	if _detail_panel and _detail_panel.has_method("populate"):
		_detail_panel.populate(_selected_file)
	_footer.set_primary_enabled(true)
	file_selected.emit(_selected_file)


func _on_item_activated(index: int) -> void:
	var meta = _item_list.get_item_metadata(index)
	if typeof(meta) != TYPE_DICTIONARY:
		return
	if meta.get("is_dir", false):
		_current_path = meta["path"]
		_populate_list()
	else:
		# Double-click on a file behaves like Next
		_on_item_selected(index)
		_on_next_pressed()


func _on_up_pressed() -> void:
	if _current_path == ROOT_PATH or _current_path == "res://":
		return
	var trimmed := _current_path
	if trimmed.ends_with("/"):
		trimmed = trimmed.substr(0, trimmed.length() - 1)
	var slash := trimmed.rfind("/")
	if slash <= 4:  # keep at least "res://"
		_current_path = ROOT_PATH
	else:
		_current_path = trimmed.substr(0, slash + 1)
	_populate_list()


func _on_next_pressed() -> void:
	if _selected_file.is_empty():
		return
	next_requested.emit()


func reset() -> void:
	_current_path = ROOT_PATH
	_selected_file = ""
	if _item_list:
		_populate_list()


func get_selected_path() -> String:
	return _selected_file