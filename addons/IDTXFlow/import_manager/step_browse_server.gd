@tool
extends VBoxContainer

## Step 2 - Browse an asset SERVER for USD files. Uses `server_mock_data`
## as a stand-in for real server responses. Layout mirrors
## `step_browse_files.gd` but the data source is different (metadata comes
## from the server response, not from disk).

signal file_selected(path: String, meta: Dictionary)
signal back_requested
signal cancel_requested
signal next_requested

const WizardTheme    := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader   := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter   := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const AssetPanel     := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")
const ServerMockData := preload("res://addons/IDTXFlow/import_manager/server_mock_data.gd")

var _server_url: String = ""
var _current_path: String = ServerMockData.ROOT_PATH
var _selected_path: String = ""
var _selected_meta: Dictionary = {}

var _path_display: LineEdit
var _item_list: ItemList
var _detail_panel: Node
var _detail_container: PanelContainer
var _footer: Node


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()
	_populate_list()


func set_server_url(url: String) -> void:
	_server_url = url
	if _path_display:
		_path_display.text = _display_path()


func reset() -> void:
	_current_path = ServerMockData.ROOT_PATH
	_selected_path = ""
	_selected_meta = {}
	if _item_list:
		_populate_list()


func get_selected_path() -> String:
	return _selected_path


func get_selected_meta() -> Dictionary:
	return _selected_meta


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(2, 3, "Browse:", "Select file to import from the asset server")

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

	_item_list.add_theme_stylebox_override("panel", WizardTheme.make_flat_style(WizardTheme.COLOR_PANEL, 0))
	var sel := WizardTheme.make_flat_style(WizardTheme.COLOR_SELECTION, 3)
	_item_list.add_theme_stylebox_override("selected", sel)
	_item_list.add_theme_stylebox_override("selected_focus", sel)
	_item_list.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	_item_list.add_theme_color_override("font_selected_color", Color.WHITE)
	_item_list.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	_item_list.add_theme_constant_override("v_separation", WizardTheme.px(6))
	_item_list.add_theme_constant_override("h_separation", WizardTheme.px(6))
	_item_list.fixed_icon_size = Vector2i(WizardTheme.px(16), WizardTheme.px(16))
	list_panel.add_child(_item_list)

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
	_footer.back_pressed.connect(_on_back)
	_footer.cancel_pressed.connect(_on_cancel)
	_footer.primary_pressed.connect(_on_next)


func _make_nav_button(text: String) -> Button:
	var b := Button.new()
	b.text = text
	var s := WizardTheme.px(WizardTheme.NAV_BTN_SIZE)
	b.custom_minimum_size = Vector2(s, s)
	WizardTheme.apply_secondary_button(b)
	return b


func _display_path() -> String:
	# Combine the base URL with the current logical path for display.
	var base := _server_url
	if base.is_empty():
		return _current_path
	if base.ends_with("/"):
		base = base.substr(0, base.length() - 1)
	return base + _current_path


func _populate_list() -> void:
	_item_list.clear()
	_selected_path = ""
	_selected_meta = {}
	_detail_container.visible = false
	_footer.set_primary_enabled(false)
	_path_display.text = _display_path()

	var entries: Array = ServerMockData.get_directory(_current_path)
	var folder_icon := WizardTheme.get_editor_icon(self, "Folder")
	var file_icon := WizardTheme.get_editor_icon(self, "PackedScene", "ResourcePreloader")

	# Directories first, then files (already declared in that order in mock data).
	for e in entries:
		if bool(e.get("is_dir", false)):
			var idx := _item_list.add_item(String(e["name"]) + "/", folder_icon)
			_item_list.set_item_metadata(idx, e)

	for e in entries:
		if not bool(e.get("is_dir", false)):
			var idx := _item_list.add_item(String(e["name"]), file_icon)
			_item_list.set_item_metadata(idx, e)


func _on_item_selected(index: int) -> void:
	var meta = _item_list.get_item_metadata(index)
	if typeof(meta) != TYPE_DICTIONARY:
		return
	if bool(meta.get("is_dir", false)):
		_selected_path = ""
		_selected_meta = {}
		_detail_container.visible = false
		_footer.set_primary_enabled(false)
		return

	_selected_path = String(meta.get("path", ""))
	_selected_meta = meta
	_detail_container.visible = true
	if _detail_panel and _detail_panel.has_method("set_header_style"):
		_detail_panel.set_header_style(0)  # ASSET_DETAILS
	if _detail_panel and _detail_panel.has_method("populate_from_dict"):
		_detail_panel.populate_from_dict(meta)
	_footer.set_primary_enabled(true)
	file_selected.emit(_selected_path, _selected_meta)


func _on_item_activated(index: int) -> void:
	var meta = _item_list.get_item_metadata(index)
	if typeof(meta) != TYPE_DICTIONARY:
		return
	if bool(meta.get("is_dir", false)):
		_current_path = String(meta.get("path", "/"))
		_populate_list()
	else:
		_on_item_selected(index)
		_on_next()


func _on_up_pressed() -> void:
	if _current_path == ServerMockData.ROOT_PATH:
		return
	var last_slash := _current_path.rfind("/")
	if last_slash <= 0:
		_current_path = ServerMockData.ROOT_PATH
	else:
		_current_path = _current_path.substr(0, last_slash)
		if _current_path.is_empty():
			_current_path = ServerMockData.ROOT_PATH
	_populate_list()


func _on_next() -> void:
	if _selected_path.is_empty():
		return
	next_requested.emit()


func _on_back() -> void:
	back_requested.emit()


func _on_cancel() -> void:
	cancel_requested.emit()