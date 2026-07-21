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

# Path history for Back/Forward navigation.
var _history: Array[String] = [ServerMockData.ROOT_PATH]
var _history_index: int = 0

# Grid vs list view.
var _is_grid_view: bool = false

var _path_display: LineEdit
var _item_list: ItemList
var _detail_panel: Node
var _detail_container: PanelContainer
var _footer: Node

var _back_btn: Button
var _forward_btn: Button
var _up_btn: Button
var _list_btn: Button
var _grid_btn: Button


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()
	_apply_view_mode()
	_populate_list()
	_update_nav_buttons()


func set_server_url(url: String) -> void:
	_server_url = url
	if _path_display:
		_path_display.text = _display_path()


func reset() -> void:
	_current_path = ServerMockData.ROOT_PATH
	_selected_path = ""
	_selected_meta = {}
	_history = [ServerMockData.ROOT_PATH]
	_history_index = 0
	if _item_list:
		_populate_list()
		_update_nav_buttons()


func get_selected_path() -> String:
	return _selected_path


func get_selected_meta() -> Dictionary:
	return _selected_meta


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(2, 4, "Browse:", "Select file to import from the asset server")

	_build_path_bar()
	_build_split()
	_build_footer()


func _build_path_bar() -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", WizardTheme.px(6))
	add_child(row)

	var cap := Label.new()
	cap.text = "Path:"
	row.add_child(cap)

	_path_display = LineEdit.new()
	_path_display.editable = false
	_path_display.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_path_display.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	row.add_child(_path_display)

	_back_btn = _make_nav_button("<", "Back")
	_back_btn.pressed.connect(_on_back_pressed)
	row.add_child(_back_btn)

	_forward_btn = _make_nav_button(">", "Forward")
	_forward_btn.pressed.connect(_on_forward_pressed)
	row.add_child(_forward_btn)

	_up_btn = _make_nav_button("^", "Parent folder")
	_up_btn.pressed.connect(_on_up_pressed)
	row.add_child(_up_btn)

	# Visual divider between nav and view-mode toggles.
	var vsep := VSeparator.new()
	vsep.modulate = Color(1, 1, 1, 0.35)
	row.add_child(vsep)

	_list_btn = _make_nav_button("☰", "List view")
	_list_btn.pressed.connect(_on_list_view_pressed)
	row.add_child(_list_btn)

	_grid_btn = _make_nav_button("⊞", "Grid view")
	_grid_btn.pressed.connect(_on_grid_view_pressed)
	row.add_child(_grid_btn)


func _build_split() -> void:
	var split := HSplitContainer.new()
	split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(split)

	var list_panel := PanelContainer.new()
	list_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list_panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(list_panel)

	_item_list = ItemList.new()
	_item_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_item_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_item_list.select_mode = ItemList.SELECT_SINGLE
	_item_list.item_selected.connect(_on_item_selected)
	_item_list.item_activated.connect(_on_item_activated)
	_item_list.fixed_icon_size = Vector2i(WizardTheme.px(16), WizardTheme.px(16))
	list_panel.add_child(_item_list)

	_detail_container = PanelContainer.new()
	_detail_container.custom_minimum_size = Vector2(WizardTheme.px(260), 0)
	_detail_container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_detail_container.visible = false
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


func _make_nav_button(text: String, tooltip: String = "") -> Button:
	var b := Button.new()
	b.text = text
	if tooltip != "":
		b.tooltip_text = tooltip
	var s := WizardTheme.px(WizardTheme.NAV_BTN_SIZE)
	b.custom_minimum_size = Vector2(s, s)
	return b


func _display_path() -> String:
	# Combine the base URL with the current logical path for display.
	var base := _server_url
	if base.is_empty():
		return _current_path
	if base.ends_with("/"):
		base = base.substr(0, base.length() - 1)
	return base + _current_path


# ---------------------------------------------------------------------------
# Listing
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# Navigation
# ---------------------------------------------------------------------------

func _navigate_to(path: String) -> void:
	if path == _current_path:
		return
	# Truncate any forward history when navigating to a new path.
	if _history_index < _history.size() - 1:
		_history.resize(_history_index + 1)
	_history.append(path)
	_history_index = _history.size() - 1
	_current_path = path
	_populate_list()
	_update_nav_buttons()


func _on_back_pressed() -> void:
	if _history_index <= 0:
		return
	_history_index -= 1
	_current_path = _history[_history_index]
	_populate_list()
	_update_nav_buttons()


func _on_forward_pressed() -> void:
	if _history_index >= _history.size() - 1:
		return
	_history_index += 1
	_current_path = _history[_history_index]
	_populate_list()
	_update_nav_buttons()


func _on_up_pressed() -> void:
	if _current_path == ServerMockData.ROOT_PATH:
		return
	var last_slash := _current_path.rfind("/")
	var parent := ServerMockData.ROOT_PATH
	if last_slash > 0:
		parent = _current_path.substr(0, last_slash)
		if parent.is_empty():
			parent = ServerMockData.ROOT_PATH
	_navigate_to(parent)


func _update_nav_buttons() -> void:
	if _back_btn:
		_back_btn.disabled = (_history_index <= 0)
	if _forward_btn:
		_forward_btn.disabled = (_history_index >= _history.size() - 1)
	if _up_btn:
		_up_btn.disabled = (_current_path == ServerMockData.ROOT_PATH)


# ---------------------------------------------------------------------------
# View mode (List / Grid)
# ---------------------------------------------------------------------------

func _on_list_view_pressed() -> void:
	if not _is_grid_view:
		return
	_is_grid_view = false
	_apply_view_mode()


func _on_grid_view_pressed() -> void:
	if _is_grid_view:
		return
	_is_grid_view = true
	_apply_view_mode()


func _apply_view_mode() -> void:
	if _item_list == null:
		return
	if _is_grid_view:
		_item_list.icon_mode = ItemList.ICON_MODE_TOP
		_item_list.max_columns = 0
		_item_list.fixed_column_width = WizardTheme.px(96)
		_item_list.fixed_icon_size = Vector2i(WizardTheme.px(48), WizardTheme.px(48))
	else:
		_item_list.icon_mode = ItemList.ICON_MODE_LEFT
		_item_list.max_columns = 1
		_item_list.fixed_column_width = 0
		_item_list.fixed_icon_size = Vector2i(WizardTheme.px(16), WizardTheme.px(16))

	if _list_btn:
		_list_btn.flat = _is_grid_view
	if _grid_btn:
		_grid_btn.flat = not _is_grid_view


# ---------------------------------------------------------------------------
# Item interaction
# ---------------------------------------------------------------------------

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
		_navigate_to(String(meta.get("path", "/")))
	else:
		_on_item_selected(index)
		_on_next()


func _on_next() -> void:
	if _selected_path.is_empty():
		return
	next_requested.emit()


func _on_back() -> void:
	back_requested.emit()


func _on_cancel() -> void:
	cancel_requested.emit()
