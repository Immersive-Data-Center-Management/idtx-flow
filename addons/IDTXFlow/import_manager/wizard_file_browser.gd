@tool
extends VBoxContainer

## Embeddable file browser that mirrors Godot's `FileDialog` layout.
##
## `FileDialog` inherits from `ConfirmationDialog`/`Window` and can't be embedded
## inline. This control ports the *interior* of `FileDialog` (`main_vbox`) into a
## plain `VBoxContainer` and reuses the same editor theme icons and theme type
## variations (`FlatButton`, `ItemListSecondary`, `HeaderSmall`) so the widget
## looks native inside the editor's main-screen tab.
##
## Kept vs. FileDialog:
##   • Top toolbar (back/forward/up/Path:/refresh)
##   • Center: view toggles, hidden files, filename filter, sort menu, ItemList,
##     filename edit + filter option
##   • Right: an optional side panel slot (used by the wizard for asset detail)
##
## Dropped: outer Window, native-dialog fallback, drives, OK/Cancel, mkdir/delete
## helper dialogs, favorites, recents. The wizard supplies Back/Cancel/Next via
## its own footer.

signal file_selected(path: String)
signal files_selected(paths: PackedStringArray)
signal dir_selected(dir: String)
signal dir_changed(dir: String)
signal filename_filter_changed(filter: String)

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")

enum FileMode { FILE_MODE_OPEN_FILE, FILE_MODE_OPEN_FILES, FILE_MODE_OPEN_DIR, FILE_MODE_OPEN_ANY }
enum DisplayMode { DISPLAY_THUMBNAILS, DISPLAY_LIST }
enum Access { ACCESS_RESOURCES, ACCESS_USERDATA, ACCESS_FILESYSTEM }
enum FileSortOption { NAME, NAME_REVERSE, TYPE, TYPE_REVERSE, MODIFIED_TIME, MODIFIED_TIME_REVERSE }

# Config
var _root_prefix: String = "res://"
var _access: int = Access.ACCESS_RESOURCES
var _file_mode: int = FileMode.FILE_MODE_OPEN_FILE
var _display_mode: int = DisplayMode.DISPLAY_LIST
var _filters: PackedStringArray = PackedStringArray()
var _filename_filter: String = ""
var _show_hidden_files: bool = false
var _show_filename_filter: bool = false
var _file_sort: int = FileSortOption.NAME
## When false, the "All Files (*.*)" catch-all entry is not appended to the
## filter OptionButton, so files that don't match any filter can never be
## selected. Defaults to true to match FileDialog's behavior.
var _all_files_option_enabled: bool = true

# Runtime
var _current_dir: String = ""
var _selected_file: String = ""
var _history: Array[String] = []
var _history_pos: int = -1

# Widgets
var _dir_prev: Button
var _dir_next: Button
var _dir_up: Button
var _directory_edit: LineEdit
var _refresh_button: Button
var _center_right_split: HSplitContainer
var _center_vbox: VBoxContainer
var _show_hidden: Button
var _thumbnail_mode_button: Button
var _list_mode_button: Button
var _show_filename_filter_button: Button
var _file_sort_button: MenuButton
var _file_list: ItemList
var _filename_filter_box: HBoxContainer
var _filename_filter_edit: LineEdit
var _file_box: HBoxContainer
var _filename_edit: LineEdit
var _filter_option: OptionButton
var _right_pane: VBoxContainer
## Built-in placeholder content shown when no side panel has been supplied
## via `set_side_panel()` — mirrors the empty ItemList used for the right pane,
## giving it visual weight even though it's currently unused.
var _details_list: ItemList
var _right_side_content: Control


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(6))


func _ready() -> void:
	if _file_list == null:
		_build()
	_update_filters_ui()
	_apply_display_mode()
	if _current_dir.is_empty():
		_current_dir = _root_prefix
	_change_dir(_current_dir, true)
	# One initial refresh: at this point we're inside the tree, so the
	# `FileDialog` theme icons should resolve. This mirrors FileDialog's
	# `_notification(NOTIFICATION_THEME_CHANGED)` behaviour.
	_refresh_toolbar_icons()


## Reapply theme-dependent state whenever the editor theme changes.
func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED:
		_refresh_toolbar_icons()


## Walk every descendant Button/MenuButton that carries the `_wfb_icon_fd`
## metadata and re-run the theme-icon lookup. Called on entering the tree
## and on every editor theme change.
func _refresh_toolbar_icons() -> void:
	if not is_inside_tree():
		return
	_refresh_icons_recursive(self)


func _refresh_icons_recursive(node: Node) -> void:
	if node is Button and node.has_meta("_wfb_icon_fd"):
		var fd: String = node.get_meta("_wfb_icon_fd")
		var fb: String = node.get_meta("_wfb_icon_fallback")
		var tex := _theme_icon(fd, fb)
		if tex:
			(node as Button).icon = _to_uniform_toolbar_icon(tex)
	for child in node.get_children():
		_refresh_icons_recursive(child)


## Resize an icon to a uniform toolbar height of `16 * editor_scale`,
## preserving aspect ratio.
static func _to_uniform_toolbar_icon(tex: Texture2D) -> Texture2D:
	if tex == null:
		return null
	var s := tex.get_size()
	if int(s.x) <= 0 or int(s.y) <= 0:
		return tex
	var target_h: int = WizardTheme.px(16)
	var scale := float(target_h) / float(s.y)
	var target_w: int = max(1, int(round(float(s.x) * scale)))
	if int(s.x) == target_w and int(s.y) == target_h:
		return tex
	var img := tex.get_image()
	if img == null:
		return tex
	if img.is_compressed():
		img.decompress()
	img.resize(target_w, target_h, Image.INTERPOLATE_LANCZOS)
	return ImageTexture.create_from_image(img)


# ==========================================================================
# Public API (mirrors FileDialog)
# ==========================================================================

func add_filter(filter: String, description: String = "", mime_type: String = "") -> void:
	assert(not filter.begins_with("."), "Filter must be \"filename.extension\".")
	var entry: String = filter
	if not description.is_empty() and not mime_type.is_empty():
		entry = "%s ; %s ; %s" % [filter, description, mime_type]
	elif not description.is_empty():
		entry = "%s ; %s" % [filter, description]
	_filters.push_back(entry)
	_update_filters_ui()
	_populate_file_list()


func clear_filters() -> void:
	_filters.clear()
	_update_filters_ui()
	_populate_file_list()


func set_filters(filters: PackedStringArray) -> void:
	_filters = filters
	_update_filters_ui()
	_populate_file_list()


func get_filters() -> PackedStringArray:
	return _filters


## Controls whether a trailing "All Files (*.*)" catch-all is added to the
## filter dropdown. Set to `false` to force the user to pick a real filter
## (e.g. when only USD files should be selectable).
func set_all_files_option_enabled(enabled: bool) -> void:
	if _all_files_option_enabled == enabled:
		return
	_all_files_option_enabled = enabled
	_update_filters_ui()
	_populate_file_list()


func is_all_files_option_enabled() -> bool:
	return _all_files_option_enabled


func set_access(access: int) -> void:
	_access = access
	match access:
		Access.ACCESS_RESOURCES:
			_root_prefix = "res://"
		Access.ACCESS_USERDATA:
			_root_prefix = "user://"
		Access.ACCESS_FILESYSTEM:
			_root_prefix = ""
	_history.clear()
	_history_pos = -1
	if _current_dir.is_empty() or (not _root_prefix.is_empty() and not _current_dir.begins_with(_root_prefix)):
		_current_dir = _root_prefix
	if is_inside_tree():
		_change_dir(_current_dir, true)


func get_access() -> int:
	return _access


func set_file_mode(mode: int) -> void:
	_file_mode = mode
	if _file_list:
		_file_list.select_mode = (
			ItemList.SELECT_MULTI if mode == FileMode.FILE_MODE_OPEN_FILES
			else ItemList.SELECT_SINGLE
		)


func get_file_mode() -> int:
	return _file_mode


func set_display_mode(mode: int) -> void:
	if _display_mode == mode:
		return
	_display_mode = mode
	_apply_display_mode()
	_populate_file_list()


func get_display_mode() -> int:
	return _display_mode


func set_current_dir(dir: String) -> void:
	_change_dir(dir, true)


func get_current_dir() -> String:
	return _current_dir


func get_current_file() -> String:
	if _filename_edit:
		return _filename_edit.text
	return ""


func get_current_path() -> String:
	return _join(_current_dir, get_current_file())


func get_selected_path() -> String:
	return _selected_file


func get_selected_files() -> PackedStringArray:
	var out: PackedStringArray = PackedStringArray()
	if _file_list == null:
		return out
	for idx in _file_list.get_selected_items():
		var meta = _file_list.get_item_metadata(idx)
		if typeof(meta) == TYPE_DICTIONARY and not meta.get("dir", true):
			out.push_back(_join(_current_dir, meta["name"]))
	return out


func set_show_hidden_files(show: bool) -> void:
	if _show_hidden_files == show:
		return
	_show_hidden_files = show
	if _show_hidden:
		_show_hidden.set_pressed_no_signal(show)
	_populate_file_list()


func is_showing_hidden_files() -> bool:
	return _show_hidden_files


func set_filename_filter(filter: String) -> void:
	if _filename_filter == filter:
		return
	_filename_filter = filter
	if _filename_filter_edit and _filename_filter_edit.text != filter:
		_filename_filter_edit.text = filter
	filename_filter_changed.emit(filter)
	_populate_file_list()


func get_filename_filter() -> String:
	return _filename_filter


## Slot for a wizard-supplied right-side panel (e.g. `AssetDetailPanel`).
##
## The panel is expected to be *self-contained* - i.e. it owns its own
## header label, background, padding etc., because the same widget is
## reused in later wizard steps (like step 4 confirm) where the wizard
## does not build a placeholder around it.
##
## When `panel != null` the whole right-pane content is cleared (built-in
## "Asset Details" header + placeholder ItemList) and the supplied panel
## takes over. Pass `null` to restore the built-in placeholder look.
func set_side_panel(panel: Control) -> void:
	if _right_pane == null:
		# Build hasn't happened yet — remember it and swap in from `_build_right_pane`.
		_right_side_content = panel
		return

# Clear the entire right pane cleanly
	for c in _right_pane.get_children():
		_right_pane.remove_child(c)
		if c != _right_side_content:
			c.queue_free() # Destroys the placeholder Label and ItemList safely!
			
	_details_list = null # Clear the reference to the destroyed list
	
	if panel:
		panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
		_right_pane.add_child(panel)
		_right_side_content = panel
	else:
		# Rebuild the built-in placeholder look:
		# a HeaderSmall label + empty ItemListSecondary underneath.
		var details_label := Label.new()
		details_label.text = "Asset Details"
		details_label.theme_type_variation = "HeaderSmall"
		_right_pane.add_child(details_label)

		_details_list = ItemList.new()
		_details_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
		_details_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		_details_list.theme_type_variation = "ItemListSecondary"
		_right_pane.add_child(_details_list)

		_right_side_content = null


func set_side_panel_visible(v: bool) -> void:
	if _right_pane:
		_right_pane.visible = v


func invalidate() -> void:
	_populate_file_list()


# ==========================================================================
# Build
# ==========================================================================

func _build() -> void:
	_build_top_toolbar()
	_build_main_split()


func _build_top_toolbar() -> void:
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", WizardTheme.px(4))
	add_child(top)

	_dir_prev = _make_flat_icon_button("back_folder", "ArrowLeft", "Go to previous folder.")
	_dir_prev.pressed.connect(_on_dir_prev_pressed)
	top.add_child(_dir_prev)

	_dir_next = _make_flat_icon_button("forward_folder", "ArrowRight", "Go to next folder.")
	_dir_next.pressed.connect(_on_dir_next_pressed)
	top.add_child(_dir_next)

	_dir_up = _make_flat_icon_button("parent_folder", "ArrowUp", "Go to parent folder.")
	_dir_up.pressed.connect(_on_dir_up_pressed)
	top.add_child(_dir_up)

	var path_label := Label.new()
	path_label.text = "Path:"
	top.add_child(path_label)

	_directory_edit = LineEdit.new()
	_directory_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_directory_edit.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	_directory_edit.text_submitted.connect(_on_directory_edit_submitted)
	top.add_child(_directory_edit)

	_refresh_button = _make_flat_icon_button("reload", "Reload", "Refresh files.")
	_refresh_button.pressed.connect(_populate_file_list)
	top.add_child(_refresh_button)


func _build_main_split() -> void:
	_center_right_split = HSplitContainer.new()
	_center_right_split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_center_right_split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_center_right_split.add_theme_constant_override("separation", WizardTheme.px(12))
	add_child(_center_right_split)

	_build_center_pane()
	_build_right_pane()


func _build_center_pane() -> void:
	_center_vbox = VBoxContainer.new()
	_center_vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_center_vbox.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_center_right_split.add_child(_center_vbox)

	# Lower toolbar (heading, hidden toggle, view mode, filter toggle, sort menu)
	var lower_toolbar := HBoxContainer.new()
	lower_toolbar.add_theme_constant_override("separation", WizardTheme.px(4))
	_center_vbox.add_child(lower_toolbar)

	var dirs_label := Label.new()
	dirs_label.text = "Directories & Files:"
	dirs_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	dirs_label.theme_type_variation = "HeaderSmall"
	lower_toolbar.add_child(dirs_label)

	_show_hidden = _make_flat_icon_button("toggle_hidden", "GuiVisibilityVisible", "Toggle the visibility of hidden files.")
	_show_hidden.toggle_mode = true
	_show_hidden.set_pressed_no_signal(_show_hidden_files)
	_show_hidden.toggled.connect(set_show_hidden_files)
	lower_toolbar.add_child(_show_hidden)

	lower_toolbar.add_child(VSeparator.new())

	_thumbnail_mode_button = _make_flat_icon_button("thumbnail_mode", "FileThumbnail", "View items as a grid of thumbnails.")
	_thumbnail_mode_button.toggle_mode = true
	_thumbnail_mode_button.pressed.connect(set_display_mode.bind(DisplayMode.DISPLAY_THUMBNAILS))
	lower_toolbar.add_child(_thumbnail_mode_button)

	_list_mode_button = _make_flat_icon_button("list_mode", "FileList", "View items as a list.")
	_list_mode_button.toggle_mode = true
	_list_mode_button.pressed.connect(set_display_mode.bind(DisplayMode.DISPLAY_LIST))
	lower_toolbar.add_child(_list_mode_button)

	lower_toolbar.add_child(VSeparator.new())

	_show_filename_filter_button = _make_flat_icon_button("toggle_filename_filter", "Search", "Toggle the visibility of the filter for file names.")
	_show_filename_filter_button.toggle_mode = true
	_show_filename_filter_button.toggled.connect(_on_filename_filter_toggle)
	lower_toolbar.add_child(_show_filename_filter_button)

	_file_sort_button = MenuButton.new()
	_file_sort_button.flat = false
	_file_sort_button.theme_type_variation = "FlatMenuButton"
	_file_sort_button.tooltip_text = "Sort files"
	_file_sort_button.set_meta("_wfb_icon_fd", "sort")
	_file_sort_button.set_meta("_wfb_icon_fallback", "Sort")
	var eager_sort := _theme_icon("sort", "Sort")
	if eager_sort:
		_file_sort_button.icon = eager_sort
	var sort_menu := _file_sort_button.get_popup()
	sort_menu.add_radio_check_item("Sort by Name (Ascending)", FileSortOption.NAME)
	sort_menu.add_radio_check_item("Sort by Name (Descending)", FileSortOption.NAME_REVERSE)
	sort_menu.add_radio_check_item("Sort by Type (Ascending)", FileSortOption.TYPE)
	sort_menu.add_radio_check_item("Sort by Type (Descending)", FileSortOption.TYPE_REVERSE)
	sort_menu.add_radio_check_item("Sort by Modified Time (Newest First)", FileSortOption.MODIFIED_TIME)
	sort_menu.add_radio_check_item("Sort by Modified Time (Oldest First)", FileSortOption.MODIFIED_TIME_REVERSE)
	sort_menu.set_item_checked(0, true)
	sort_menu.id_pressed.connect(_on_sort_option_selected)
	lower_toolbar.add_child(_file_sort_button)

	# Main file list
	_file_list = ItemList.new()
	_file_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_file_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_file_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	_file_list.theme_type_variation = "ItemListSecondary"
	_file_list.select_mode = ItemList.SELECT_SINGLE
	_file_list.item_selected.connect(_on_file_item_selected)
	_file_list.item_activated.connect(_on_file_item_activated)
	_center_vbox.add_child(_file_list)

	# Filename filter row (initially hidden)
	_filename_filter_box = HBoxContainer.new()
	_filename_filter_box.visible = false
	_center_vbox.add_child(_filename_filter_box)

	var filter_cap := Label.new()
	filter_cap.text = "Filter:"
	_filename_filter_box.add_child(filter_cap)

	_filename_filter_edit = LineEdit.new()
	_filename_filter_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_filename_filter_edit.clear_button_enabled = true
	_filename_filter_edit.text_changed.connect(set_filename_filter)
	_filename_filter_box.add_child(_filename_filter_edit)

	# Bottom "File:" row with filename edit and filter option
	_file_box = HBoxContainer.new()
	_center_vbox.add_child(_file_box)

	var file_cap := Label.new()
	file_cap.text = "File:"
	_file_box.add_child(file_cap)

	_filename_edit = LineEdit.new()
	_filename_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_filename_edit.set_stretch_ratio(4.0)
	_filename_edit.focus_mode = Control.FOCUS_NONE
	_filename_edit.mouse_filter = Control.MOUSE_FILTER_IGNORE
	
	_file_box.add_child(_filename_edit)

	_filter_option = OptionButton.new()
	_filter_option.set_stretch_ratio(3.0)
	_filter_option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_filter_option.clip_text = true
	_filter_option.item_selected.connect(_on_filter_option_selected)
	_file_box.add_child(_filter_option)


func _build_right_pane() -> void:
	_right_pane = VBoxContainer.new()
	_right_pane.custom_minimum_size = Vector2(WizardTheme.px(250), 0)
	_right_pane.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_center_right_split.add_child(_right_pane)

	# If a wizard set a side panel before we were built, honour it directly.
	if _right_side_content:
		_right_side_content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_right_side_content.size_flags_vertical = Control.SIZE_EXPAND_FILL
		_right_pane.add_child(_right_side_content)
	else:
		# Otherwise, build the default placeholders
		var details_label := Label.new()
		details_label.text = "Asset Details"
		details_label.theme_type_variation = "HeaderSmall"
		_right_pane.add_child(details_label)

		_details_list = ItemList.new()
		_details_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
		_details_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		_details_list.theme_type_variation = "ItemListSecondary"
		_right_pane.add_child(_details_list)
		
# ==========================================================================
# Navigation / listing
# ==========================================================================

func _change_dir(path: String, push_history: bool = true) -> void:
	var normalized := _normalize_dir(path)
	if not _root_prefix.is_empty() and not normalized.begins_with(_root_prefix):
		return
	if not DirAccess.dir_exists_absolute(normalized):
		normalized = _root_prefix
		if not DirAccess.dir_exists_absolute(normalized):
			return

	_current_dir = normalized
	if _directory_edit:
		_directory_edit.text = normalized
	if push_history:
		_push_history(normalized)
	_update_nav_buttons()
	_populate_file_list()
	dir_changed.emit(normalized)


func _normalize_dir(path: String) -> String:
	var p := path
	if p.is_empty():
		p = _root_prefix
	# Collapse trailing slashes, keep exactly one.
	while p.length() > 1 and p.ends_with("/") and not p.ends_with("://"):
		p = p.substr(0, p.length() - 1)
	if not p.ends_with("/"):
		p += "/"
	return p


func _push_history(path: String) -> void:
	if _history_pos < _history.size() - 1:
		_history = _history.slice(0, _history_pos + 1)
	if _history.is_empty() or _history[_history.size() - 1] != path:
		_history.push_back(path)
		_history_pos = _history.size() - 1


func _update_nav_buttons() -> void:
	if _dir_prev:
		_dir_prev.disabled = _history_pos <= 0
	if _dir_next:
		_dir_next.disabled = _history_pos >= _history.size() - 1
	if _dir_up:
		_dir_up.disabled = _current_dir == _root_prefix or _current_dir.is_empty()


func _populate_file_list() -> void:
	if _file_list == null:
		return
	_file_list.clear()
	_selected_file = ""

	var dir := DirAccess.open(_current_dir)
	if dir == null:
		return
	dir.include_navigational = false
	dir.include_hidden = _show_hidden_files

	var dirs: PackedStringArray = PackedStringArray()
	var files: PackedStringArray = PackedStringArray()

	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if _show_hidden_files or not entry.begins_with("."):
			if dir.current_is_dir():
				dirs.push_back(entry)
			else:
				files.push_back(entry)
		entry = dir.get_next()
	dir.list_dir_end()

	# Apply filters + filename filter
	var patterns := _active_filter_patterns()
	var filtered_files: Array[String] = []
	for f in files:
		if not patterns.is_empty() and not _matches_any(f, patterns):
			continue
		if not _filename_filter.is_empty() and not f.to_lower().contains(_filename_filter.to_lower()):
			continue
		filtered_files.append(f)

	var filtered_dirs: Array[String] = []
	for d in dirs:
		if not _filename_filter.is_empty() and not d.to_lower().contains(_filename_filter.to_lower()):
			continue
		filtered_dirs.append(d)

	_sort_names(filtered_dirs, true)
	_sort_names(filtered_files, false)

	var folder_icon := _theme_icon("folder", "Folder")
	var file_icon := _theme_icon("file", "File")
	var folder_thumb := _theme_icon("folder_thumbnail", "Folder")
	var file_thumb := _theme_icon("file_thumbnail", "File")

	for d in filtered_dirs:
		var idx := _file_list.add_item(d, folder_thumb if _display_mode == DisplayMode.DISPLAY_THUMBNAILS else folder_icon)
		_file_list.set_item_metadata(idx, {"name": d, "dir": true})

	for f in filtered_files:
		var idx2 := _file_list.add_item(f, file_thumb if _display_mode == DisplayMode.DISPLAY_THUMBNAILS else file_icon)
		_file_list.set_item_metadata(idx2, {"name": f, "dir": false})


func _sort_names(arr: Array[String], is_dirs: bool) -> void:
	match _file_sort:
		FileSortOption.NAME, FileSortOption.MODIFIED_TIME, FileSortOption.TYPE:
			arr.sort()
		FileSortOption.NAME_REVERSE, FileSortOption.MODIFIED_TIME_REVERSE, FileSortOption.TYPE_REVERSE:
			arr.sort()
			arr.reverse()
		_:
			arr.sort()

	if not is_dirs and (_file_sort == FileSortOption.TYPE or _file_sort == FileSortOption.TYPE_REVERSE):
		arr.sort_custom(func(a, b):
			return a.get_extension().to_lower() < b.get_extension().to_lower()
		)
		if _file_sort == FileSortOption.TYPE_REVERSE:
			arr.reverse()

	if not is_dirs and (_file_sort == FileSortOption.MODIFIED_TIME or _file_sort == FileSortOption.MODIFIED_TIME_REVERSE):
		var base := _current_dir
		arr.sort_custom(func(a, b):
			return FileAccess.get_modified_time(_join(base, a)) < FileAccess.get_modified_time(_join(base, b))
		)
		if _file_sort == FileSortOption.MODIFIED_TIME_REVERSE:
			arr.reverse()


# ==========================================================================
# Filter parsing (FileDialog syntax: "*.png,*.jpg;Description;mime/type")
# ==========================================================================

func _active_filter_patterns() -> PackedStringArray:
	if _filter_option == null or _filters.is_empty():
		return PackedStringArray()

	var out: PackedStringArray = PackedStringArray()
	var sel := _filter_option.get_selected()

	if _all_files_option_enabled and sel == _filter_option.item_count - 1:
		return PackedStringArray()

	if _filters.size() > 1 and sel == 0:
		for entry in _filters:
			for pat in entry.get_slice(";", 0).split(","):
				out.push_back(pat.strip_edges())
		return out

	var idx := sel
	if _filters.size() > 1:
		idx -= 1
	if idx >= 0 and idx < _filters.size():
		for pat in _filters[idx].get_slice(";", 0).split(","):
			out.push_back(pat.strip_edges())
	return out


func _matches_any(name: String, patterns: PackedStringArray) -> bool:
	for p in patterns:
		if name.matchn(p):
			return true
	return false


func _update_filters_ui() -> void:
	if _filter_option == null:
		return
	_filter_option.clear()

	if _filters.size() > 1:
		var summary: PackedStringArray = PackedStringArray()
		for entry in _filters:
			summary.push_back(entry.get_slice(";", 0).strip_edges())
		_filter_option.add_item("All Recognized (" + ", ".join(summary) + ")")

	for entry in _filters:
		var flt := entry.get_slice(";", 0).strip_edges()
		var desc := entry.get_slice(";", 1).strip_edges()
		if desc.is_empty():
			_filter_option.add_item("(" + flt + ")")
		else:
			_filter_option.add_item(desc + " (" + flt + ")")

	if _all_files_option_enabled:
		_filter_option.add_item("All Files (*.*)")


# ==========================================================================
# Handlers
# ==========================================================================

func _on_dir_prev_pressed() -> void:
	if _history_pos <= 0:
		return
	_history_pos -= 1
	_change_dir(_history[_history_pos], false)


func _on_dir_next_pressed() -> void:
	if _history_pos >= _history.size() - 1:
		return
	_history_pos += 1
	_change_dir(_history[_history_pos], false)


func _on_dir_up_pressed() -> void:
	if _current_dir == _root_prefix:
		return
	var trimmed := _current_dir
	if trimmed.ends_with("/"):
		trimmed = trimmed.substr(0, trimmed.length() - 1)
	var slash := trimmed.rfind("/")
	var parent := _root_prefix
	if slash > 4:
		parent = trimmed.substr(0, slash + 1)
	_change_dir(parent, true)


func _on_directory_edit_submitted(text: String) -> void:
	_change_dir(text, true)


func _on_file_item_selected(index: int) -> void:
	var meta = _file_list.get_item_metadata(index)
	if typeof(meta) != TYPE_DICTIONARY:
		return
	if meta.get("dir", false):
		_selected_file = ""
		if _filename_edit:
			_filename_edit.text = ""
		return
	_selected_file = _join(_current_dir, meta["name"])
	if _filename_edit:
		_filename_edit.text = meta["name"]
	file_selected.emit(_selected_file)


func _on_file_item_activated(index: int) -> void:
	var meta = _file_list.get_item_metadata(index)
	if typeof(meta) != TYPE_DICTIONARY:
		return
	if meta.get("dir", false):
		_change_dir(_join(_current_dir, meta["name"]), true)
		return
	_selected_file = _join(_current_dir, meta["name"])
	if _filename_edit:
		_filename_edit.text = meta["name"]
	file_selected.emit(_selected_file)


func _on_filter_option_selected(_idx: int) -> void:
	_populate_file_list()


func _on_filename_filter_toggle(pressed: bool) -> void:
	_show_filename_filter = pressed
	if _filename_filter_box:
		_filename_filter_box.visible = pressed
	if not pressed:
		set_filename_filter("")
	elif _filename_filter_edit:
		_filename_filter_edit.grab_focus()


func _on_sort_option_selected(id: int) -> void:
	_file_sort = id
	if _file_sort_button:
		var popup := _file_sort_button.get_popup()
		for i in popup.item_count:
			popup.set_item_checked(i, popup.get_item_id(i) == id)
	_populate_file_list()


# ==========================================================================
# View mode
# ==========================================================================

func _apply_display_mode() -> void:
	if _file_list == null:
		return
	if _display_mode == DisplayMode.DISPLAY_THUMBNAILS:
		_file_list.icon_mode = ItemList.ICON_MODE_TOP
		_file_list.max_columns = 0
		_file_list.fixed_column_width = WizardTheme.px(96)
		_file_list.fixed_icon_size = Vector2i(WizardTheme.px(64), WizardTheme.px(64))
		_file_list.max_text_lines = 2
	else:
		_file_list.icon_mode = ItemList.ICON_MODE_LEFT
		_file_list.max_columns = 1
		_file_list.fixed_column_width = 0
		_file_list.fixed_icon_size = Vector2i(WizardTheme.px(16), WizardTheme.px(16))
		_file_list.max_text_lines = 1

	if _thumbnail_mode_button:
		_thumbnail_mode_button.set_pressed_no_signal(_display_mode == DisplayMode.DISPLAY_THUMBNAILS)
	if _list_mode_button:
		_list_mode_button.set_pressed_no_signal(_display_mode == DisplayMode.DISPLAY_LIST)


# ==========================================================================
# Helpers
# ==========================================================================

## Create a flat icon button that uses a `FileDialog` theme icon by name (with
## an `EditorIcons` fallback), matching how FileDialog itself styles its
## toolbar buttons.
func _make_flat_icon_button(file_dialog_icon: String, editor_fallback: String, tooltip: String = "") -> Button:
	var b := Button.new()
	b.theme_type_variation = "FlatButton"
	b.focus_mode = Control.FOCUS_NONE
	b.tooltip_text = tooltip
	b.set_meta("_wfb_icon_fd", file_dialog_icon)
	b.set_meta("_wfb_icon_fallback", editor_fallback)
	var eager := _theme_icon(file_dialog_icon, editor_fallback)
	if eager:
		b.icon = eager
	return b


## Lookup helper: try `FileDialog`'s theme icon first (so we get the exact
## same icons FileDialog uses), then fall back to an `EditorIcons` name.
func _theme_icon(file_dialog_icon: String, editor_fallback: String) -> Texture2D:
	if is_inside_tree() and has_theme_icon(file_dialog_icon, "FileDialog"):
		return get_theme_icon(file_dialog_icon, "FileDialog")
	return WizardTheme.get_editor_icon(self, editor_fallback)


func _join(base: String, name: String) -> String:
	if base.is_empty():
		return name
	if base.ends_with("/"):
		return base + name
	return base + "/" + name
