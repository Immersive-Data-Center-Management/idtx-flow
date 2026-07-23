@tool
extends VBoxContainer

## Step 2 - Browse the asset SERVER for USD files.
##
## Backed by the real IDTX backend via the `IdtxClient` engine singleton
## (GET /api/v1/files). The backend returns a flat file list; we present it
## grouped by the `directory` field: a non-selectable directory header row
## followed by its files. Selecting a file emits `file_selected(path, meta)`
## and enables Next; the selection contract (get_selected_path/meta) is
## unchanged so import_manager.gd keeps working.

signal file_selected(path: String, meta: Dictionary)
signal back_requested
signal cancel_requested
signal next_requested

const WizardTheme    := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader   := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter   := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const AssetPanel     := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")

var _server_url: String = ""
var _selected_path: String = ""
var _selected_meta: Dictionary = {}
var _loading: bool = false

# Grid vs list view.
var _is_grid_view: bool = false

var _path_display: LineEdit
var _item_list: ItemList
var _detail_panel: Node
var _detail_container: PanelContainer
var _footer: Node
var _status_label: Label

var _refresh_btn: Button
var _list_btn: Button
var _grid_btn: Button


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()
	_apply_view_mode()
	_refresh()


func set_server_url(url: String) -> void:
	_server_url = url
	if _path_display:
		_path_display.text = _display_path()


func reset() -> void:
	_selected_path = ""
	_selected_meta = {}
	if _item_list:
		_refresh()


func get_selected_path() -> String:
	return _selected_path


func get_selected_meta() -> Dictionary:
	return _selected_meta


func _idtx() -> Object:
	if not Engine.has_singleton("IdtxClient"):
		return null
	return Engine.get_singleton("IdtxClient")


# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------

func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(2, 4, "Browse:", "Select a file to import from the asset server")

	_build_path_bar()
	_build_split()
	_build_footer()


func _build_path_bar() -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", WizardTheme.px(6))
	add_child(row)

	var cap := Label.new()
	cap.text = "Server:"
	row.add_child(cap)

	_path_display = LineEdit.new()
	_path_display.editable = false
	_path_display.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_path_display.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	row.add_child(_path_display)

	_refresh_btn = _make_nav_button("⟳", "Refresh")
	_refresh_btn.pressed.connect(_refresh)
	row.add_child(_refresh_btn)

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
	_status_label = Label.new()
	_status_label.modulate = Color(1, 1, 1, 0.6)
	add_child(_status_label)

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
	return _server_url


# ---------------------------------------------------------------------------
# Listing (real /files, grouped by directory)
# ---------------------------------------------------------------------------

func _refresh() -> void:
	if _item_list == null:
		return
	_item_list.clear()
	_selected_path = ""
	_selected_meta = {}
	_detail_container.visible = false
	if _footer:
		_footer.set_primary_enabled(false)
	_path_display.text = _display_path()

	var client := _idtx()
	if client == null:
		_set_status("IDTX client not available (GDExtension not loaded).")
		return

	if _loading:
		return
	_loading = true
	_set_status("Loading files…")

	client.files_listed.connect(_on_files_listed, CONNECT_ONE_SHOT)
	client.request_failed.connect(_on_request_failed, CONNECT_ONE_SHOT)
	client.list_files("", "")


func _on_files_listed(files: Array) -> void:
	_loading = false
	_disconnect_list_handlers()
	_populate_from_files(files)
	_set_status("%d file(s)" % files.size())


func _on_request_failed(op: String, http_code: int, code: String, message: String) -> void:
	# Only react to list_files failures here.
	if op != "list_files":
		return
	_loading = false
	_disconnect_list_handlers()
	var msg := message
	if msg.is_empty():
		msg = "%d %s" % [http_code, code]
	_set_status("Failed to list files: %s" % msg)


func _disconnect_list_handlers() -> void:
	var client := _idtx()
	if client == null:
		return
	if client.files_listed.is_connected(_on_files_listed):
		client.files_listed.disconnect(_on_files_listed)
	if client.request_failed.is_connected(_on_request_failed):
		client.request_failed.disconnect(_on_request_failed)


func _populate_from_files(files: Array) -> void:
	_item_list.clear()

	var folder_icon := WizardTheme.get_editor_icon(self, "Folder")
	var file_icon := WizardTheme.get_editor_icon(self, "PackedScene", "ResourcePreloader")

	# Group entries by their 'directory' field. The backend may return Windows
	# separators (e.g. "Teapot\geo"); normalize to forward slashes for display
	# and, more importantly, for the paths we send back to /sessions and /download.
	var groups := {}   # directory -> Array of entry dicts
	for f in files:
		if typeof(f) != TYPE_DICTIONARY:
			continue
		var directory := String(f.get("directory", "")).replace("\\", "/")
		if not groups.has(directory):
			groups[directory] = []
		groups[directory].append(f)

	# Sort directory names ("" root first, then alphabetical).
	var dir_names := groups.keys()
	dir_names.sort_custom(func(a, b):
		if a == "":
			return true
		if b == "":
			return false
		return String(a) < String(b))

	for directory in dir_names:
		# Directory header row (non-selectable).
		var header_text: String = "/" if String(directory).is_empty() else (String(directory) + "/")
		var hidx := _item_list.add_item(header_text, folder_icon)
		_item_list.set_item_selectable(hidx, false)
		_item_list.set_item_metadata(hidx, {"is_dir": true})

		# File rows under this directory, sorted by filename.
		var entries: Array = groups[directory]
		entries.sort_custom(func(a, b):
			return String(a.get("filename", "")) < String(b.get("filename", "")))

		for f in entries:
			var filename := String(f.get("filename", ""))
			# Normalize Windows separators and any stray leading slash so the
			# path matches the backend's /sessions + /download contract.
			var filepath := String(f.get("filepath", "")).replace("\\", "/").lstrip("/")
			# Build the entry dict expected by the detail panel / selection contract.
			var meta := {
				"name": filename,
				"path": filepath,             # used by get_selected_path()
				"is_dir": false,
				"size_bytes": int(f.get("size", 0)),
				"modified": _format_modified(f.get("modified", 0)),
				"description": String(directory),
			}
			var idx := _item_list.add_item("    " + filename, file_icon)
			_item_list.set_item_metadata(idx, meta)


func _set_status(msg: String) -> void:
	if _status_label:
		_status_label.text = msg


## The backend `modified` is an opaque numeric timestamp
## (file_time_type::time_since_epoch().count()). We can't reliably interpret its
## unit, so present a readable date when the value plausibly looks like Unix
## seconds/millis, otherwise just stringify it. (Used for display only.)
func _format_modified(value) -> String:
	var n := int(value)
	if n <= 0:
		return ""
	var secs := n
	# Heuristic: values far larger than "now in seconds" are millis/nanos.
	if secs > 100_000_000_000:          # > ~year 5138 in seconds → likely millis
		secs = int(secs / 1000)
	if secs > 100_000_000_000:          # still huge → likely micros
		secs = int(secs / 1000)
	if secs > 100_000_000_000:          # still huge → likely nanos
		secs = int(secs / 1000)
	if secs > 1_000_000_000 and secs < 100_000_000_000:
		return Time.get_datetime_string_from_unix_time(secs).substr(0, 10)
	# Fallback: opaque value, show as-is.
	return str(value)


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
		return  # directory headers are not navigable (flat, grouped list)
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
