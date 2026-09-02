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

signal file_selected(path: String, meta: Dictionary)
signal files_selected(paths: PackedStringArray, metas: Array)
signal dir_selected(dir: String)
signal dir_changed(dir: String)
signal filename_filter_changed(filter: String)
## Async listing status surface. Emitted with a human-readable message when a
## provider starts loading, finishes, or fails, so wizard steps can show a
## small status label (server browse relies on this).
signal listing_status(message: String)

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
# Providers are resolved with load() at runtime (not preload consts) to avoid
# parse-time dependency ordering issues when the plugin is first compiled —
# same pattern import_manager.gd uses for its step scripts.
const LOCAL_FILE_PROVIDER_PATH := "res://addons/IDTXFlow/import_manager/local_file_provider.gd"

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

# Data source. Defaults to a local (res://) provider so the local browse step
# keeps working with no extra wiring. Server browse swaps in a ServerFileProvider.
var _provider: RefCounted = null

# Runtime
var _current_dir: String = ""
var _selected_file: String = ""
## Metadata dict for the currently selected file (empty for local files).
var _selected_meta: Dictionary = {}
var _history: Array[String] = []
var _history_pos: int = -1
## Cache of the entries returned by the last `entries_ready` for `_current_dir`,
## so filter/sort/view changes can re-render without re-listing.
var _last_entries: Array = []

# Grid thumbnails (thumbnail display mode, thumbnail-capable providers only):
#   _thumb_index    : usd_file -> current ItemList index, rebuilt every render so
#                     late results for a since-changed listing are ignored.
#   _thumb_tex_cache: usd_file -> decoded ImageTexture, so re-renders (sort /
#                     filter / mode toggle) don't re-decode. The network dedup is
#                     handled by the engine-agnostic byte cache in the core.
var _thumb_index: Dictionary = {}
var _thumb_tex_cache: Dictionary = {}

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
	if _provider == null:
		# Default to a local (res://) provider so the local browse step works
		# with no extra wiring.
		set_file_provider((load(LOCAL_FILE_PROVIDER_PATH) as GDScript).new())
	_update_filters_ui()
	_apply_display_mode()
	_apply_navigation_support()
	if _current_dir.is_empty():
		_current_dir = _root_prefix
	_change_dir(_current_dir, true)
	# One initial refresh: at this point we're inside the tree, so the
	# `FileDialog` theme icons should resolve. This mirrors FileDialog's
	# `_notification(NOTIFICATION_THEME_CHANGED)` behaviour.
	_refresh_toolbar_icons()


## Swap in the data source. Connects the provider's async listing signals and
## adopts its root prefix + navigation support. Pass a `FileProvider` subclass
## instance (LocalFileProvider by default, ServerFileProvider for the server
## browse step).
func set_file_provider(provider: RefCounted) -> void:
	if _provider == provider:
		return
	if _provider:
		if _provider.entries_ready.is_connected(_on_provider_entries_ready):
			_provider.entries_ready.disconnect(_on_provider_entries_ready)
		if _provider.list_failed.is_connected(_on_provider_list_failed):
			_provider.list_failed.disconnect(_on_provider_list_failed)
		if _provider.thumbnail_ready.is_connected(_on_provider_thumbnail_ready):
			_provider.thumbnail_ready.disconnect(_on_provider_thumbnail_ready)
	_provider = provider
	if _provider:
		_provider.entries_ready.connect(_on_provider_entries_ready)
		_provider.list_failed.connect(_on_provider_list_failed)
		_provider.thumbnail_ready.connect(_on_provider_thumbnail_ready)
		# A new provider's thumbnails are unrelated; drop any decoded textures.
		_thumb_tex_cache.clear()
		_thumb_index.clear()
		_root_prefix = _provider.get_root_prefix()
		_history.clear()
		_history_pos = -1
		if _current_dir.is_empty() or (not _root_prefix.is_empty() and not _current_dir.begins_with(_root_prefix)):
			_current_dir = _root_prefix
	_apply_navigation_support()
	if is_inside_tree() and _file_list:
		_change_dir(_current_dir, true)


func get_file_provider() -> RefCounted:
	return _provider


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
	_rerender_cached()


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


## Metadata dict for the current selection (empty for local files, the backend
## dict for server files). Kept so wizard steps can forward it to their detail
## panel / import state.
func get_selected_meta() -> Dictionary:
	return _selected_meta


func get_selected_files() -> PackedStringArray:
	var out: PackedStringArray = PackedStringArray()
	if _file_list == null:
		return out
	for idx in _file_list.get_selected_items():
		var e = _file_list.get_item_metadata(idx)
		if typeof(e) == TYPE_DICTIONARY and not bool(e.get("is_dir", true)):
			out.push_back(String(e.get("path", "")))
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
	_rerender_cached()


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
	_refresh_button.pressed.connect(_on_refresh_pressed)
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
	var supports_nav: bool = _provider == null or _provider.supports_navigation()
	# Only enforce the root-prefix boundary / existence check when we're
	# actually navigating a tree. Flat providers (server) always list their
	# single root regardless of the typed path.
	if supports_nav:
		if not _root_prefix.is_empty() and not normalized.begins_with(_root_prefix):
			return
		if _provider and not _provider.dir_exists(normalized):
			normalized = _root_prefix
			if not _provider.dir_exists(normalized):
				return

	_current_dir = normalized
	if _directory_edit:
		_directory_edit.text = normalized
	if push_history and supports_nav:
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


## Re-apply sort/filter/display to the already-fetched listing without a network
## round-trip. Sort, extension/filename filters and the grid/list layout are all
## client-side transforms of the same entries, so they never need a re-list.
## Falls back to a fresh fetch if no cached entries exist yet.
func _rerender_cached() -> void:
	if _provider == null or _last_entries.is_empty():
		_populate_file_list()
		return
	_render_entries(_last_entries)
	var file_count := 0
	for e in _last_entries:
		if not bool(e.get("is_dir", false)):
			file_count += 1
	listing_status.emit("%d file(s)" % file_count)


## Refresh button: drop any cached listing/tree in the provider (so new uploads
## / server changes appear), then re-list the current directory.
func _on_refresh_pressed() -> void:
	if _provider and _provider.has_method("request_reload"):
		_provider.request_reload()
	_populate_file_list()


## Ask the current provider to (re)list `_current_dir`. Rendering happens
## asynchronously in `_on_provider_entries_ready`.
func _populate_file_list() -> void:
	if _file_list == null:
		return
	if _provider == null:
		return
	listing_status.emit("Loading…")
	_provider.list_dir(_current_dir)


## Provider callback: filter, sort and render the entries it returned.
## Ignores stale responses for a directory we've since navigated away from.
func _on_provider_entries_ready(dir: String, entries: Array) -> void:
	if dir != _current_dir:
		return
	_last_entries = entries
	_render_entries(entries)
	# Count only selectable file rows for the status line.
	var file_count := 0
	for e in entries:
		if not bool(e.get("is_dir", false)):
			file_count += 1
	listing_status.emit("%d file(s)" % file_count)


func _on_provider_list_failed(dir: String, message: String) -> void:
	if dir != _current_dir:
		return
	if _file_list:
		_file_list.clear()
	_last_entries = []
	_selected_file = ""
	_selected_meta = {}
	listing_status.emit(message)


## Apply filters + filename filter + sort to the provider's entries and paint
## the ItemList. Directory entries are grouped/sorted separately from files,
## and non-selectable entries (e.g. server directory headers) are marked as
## such. Each item's metadata is the full entry dict.
func _render_entries(entries: Array) -> void:
	if _file_list == null:
		return
	_file_list.clear()
	_selected_file = ""
	_selected_meta = {}
	# Rebuilt below; entries not present here after a render are treated as stale
	# by the thumbnail-ready handler.
	_thumb_index = {}

	var patterns := _active_filter_patterns()

	var dir_entries: Array = []
	var file_entries: Array = []
	for e in entries:
		var name := String(e.get("name", ""))
		var is_dir := bool(e.get("is_dir", false))
		if is_dir:
			# Directory headers/folders bypass extension filters but still
			# respect the filename filter (unless non-selectable headers).
			if bool(e.get("selectable", true)):
				if not _filename_filter.is_empty() and not name.to_lower().contains(_filename_filter.to_lower()):
					continue
			dir_entries.append(e)
		else:
			if not patterns.is_empty() and not _matches_any(name, patterns):
				continue
			if not _filename_filter.is_empty() and not name.to_lower().contains(_filename_filter.to_lower()):
				continue
			file_entries.append(e)

	# Only sort when the provider supports navigation (a real tree). Flat
	# providers (server) supply a curated grouped order we preserve as-is.
	if _provider == null or _provider.supports_navigation():
		_sort_entries(dir_entries, true)
		_sort_entries(file_entries, false)

	var folder_icon := _theme_icon("folder", "Folder")
	var file_icon := _theme_icon("file", "File")
	var folder_thumb := _theme_icon("folder_thumbnail", "Folder")
	var file_thumb := _theme_icon("file_thumbnail", "File")

	# For flat/grouped providers, headers and their files are interleaved in
	# the source order (preserving the provider's grouping); for navigable
	# trees we show dirs first then files.
	var ordered: Array = []
	if _provider and not _provider.supports_navigation():
		ordered = _filter_flat_entries(entries, patterns)
	else:
		ordered = dir_entries + file_entries

	for e in ordered:
		var is_dir := bool(e.get("is_dir", false))
		var name := String(e.get("name", ""))
		var tex: Texture2D
		if is_dir:
			tex = folder_thumb if _display_mode == DisplayMode.DISPLAY_THUMBNAILS else folder_icon
		else:
			tex = file_thumb if _display_mode == DisplayMode.DISPLAY_THUMBNAILS else file_icon
		var idx := _file_list.add_item(name, tex)
		_file_list.set_item_metadata(idx, e)
		if not bool(e.get("selectable", true)):
			_file_list.set_item_selectable(idx, false)
		elif not is_dir:
			# Thumbnails load in both layouts (grid + list); the grid/list toggle
			# is a layout switch only. No-ops for providers without thumbnails.
			_maybe_request_thumbnail(e, idx)


## Ask the provider for `entry`'s thumbnail (thumbnail mode + thumbnail-capable
## providers only). If we already decoded it this session, apply the cached
## texture immediately; otherwise record the row index and kick off the async
## request (resolved in `_on_provider_thumbnail_ready`).
func _maybe_request_thumbnail(entry: Dictionary, idx: int) -> void:
	if _provider == null or not _provider.supports_thumbnails():
		return
	var meta: Dictionary = entry.get("meta", {})
	var usd_file := String(meta.get("path", entry.get("path", "")))
	if usd_file.is_empty():
		return
	_thumb_index[usd_file] = idx
	if _thumb_tex_cache.has(usd_file):
		_file_list.set_item_icon(idx, _thumb_tex_cache[usd_file])
		return
	_provider.request_thumbnail(usd_file)


func _on_provider_thumbnail_ready(usd_file: String, bytes: PackedByteArray, content_type: String) -> void:
	var tex := _decode_image(bytes, content_type)
	if tex == null:
		return
	_thumb_tex_cache[usd_file] = tex
	# Apply only if this file is still shown at a known row (else it's stale).
	if _thumb_index.has(usd_file):
		var idx: int = _thumb_index[usd_file]
		if _file_list and idx >= 0 and idx < _file_list.item_count:
			_file_list.set_item_icon(idx, tex)


## Decode PNG/JPEG bytes into a texture, or null on failure.
func _decode_image(bytes: PackedByteArray, content_type: String) -> Texture2D:
	if bytes.is_empty():
		return null
	var img := Image.new()
	var err := ERR_UNAVAILABLE
	if content_type.contains("png"):
		err = img.load_png_from_buffer(bytes)
	elif content_type.contains("jpeg") or content_type.contains("jpg"):
		err = img.load_jpg_from_buffer(bytes)
	else:
		err = img.load_png_from_buffer(bytes)
		if err != OK:
			err = img.load_jpg_from_buffer(bytes)
	if err != OK:
		return null
	return ImageTexture.create_from_image(img)


## Filter a flat/grouped entry list in place-order: keep all directory headers,
## keep files that pass the extension + filename filters. Preserves the
## provider's grouping order.
func _filter_flat_entries(entries: Array, patterns: PackedStringArray) -> Array:
	var out: Array = []
	for e in entries:
		var is_dir := bool(e.get("is_dir", false))
		if is_dir:
			out.append(e)
			continue
		var name := String(e.get("name", ""))
		if not patterns.is_empty() and not _matches_any(name, patterns):
			continue
		if not _filename_filter.is_empty() and not name.to_lower().contains(_filename_filter.to_lower()):
			continue
		out.append(e)
	return out


func _sort_entries(arr: Array, is_dirs: bool) -> void:
	# Base: alphabetical by name. Uses named comparator methods rather than
	# inline lambdas — multi-line lambdas inside `match` arms don't parse in
	# GDScript, and named methods keep this readable.
	arr.sort_custom(_cmp_name_asc)

	var reverse: bool = (
		_file_sort == FileSortOption.NAME_REVERSE
		or _file_sort == FileSortOption.TYPE_REVERSE
		or _file_sort == FileSortOption.MODIFIED_TIME_REVERSE
	)

	if not is_dirs and (_file_sort == FileSortOption.TYPE or _file_sort == FileSortOption.TYPE_REVERSE):
		arr.sort_custom(_cmp_ext_asc)
	elif not is_dirs and (_file_sort == FileSortOption.MODIFIED_TIME or _file_sort == FileSortOption.MODIFIED_TIME_REVERSE):
		arr.sort_custom(_cmp_mtime_asc)

	if reverse:
		arr.reverse()


func _cmp_name_asc(a, b) -> bool:
	return String(a.get("name", "")) < String(b.get("name", ""))


func _cmp_ext_asc(a, b) -> bool:
	return String(a.get("name", "")).get_extension().to_lower() < String(b.get("name", "")).get_extension().to_lower()


func _cmp_mtime_asc(a, b) -> bool:
	var am := int(a.get("meta", {}).get("modified_unix", 0))
	var bm := int(b.get("meta", {}).get("modified_unix", 0))
	return am < bm


## Show/hide the navigation chrome based on whether the current provider can
## navigate a tree. Flat providers (server) get a read-only path field and no
## back/forward/up buttons.
func _apply_navigation_support() -> void:
	var nav: bool = _provider == null or _provider.supports_navigation()
	if _dir_prev:
		_dir_prev.visible = nav
	if _dir_next:
		_dir_next.visible = nav
	if _dir_up:
		_dir_up.visible = nav
	if _directory_edit:
		_directory_edit.editable = nav


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
	var e = _file_list.get_item_metadata(index)
	if typeof(e) != TYPE_DICTIONARY:
		return
	if bool(e.get("is_dir", false)):
		_selected_file = ""
		_selected_meta = {}
		if _filename_edit:
			_filename_edit.text = ""
		return
	_selected_file = String(e.get("path", ""))
	_selected_meta = e.get("meta", {})
	if _filename_edit:
		_filename_edit.text = String(e.get("name", ""))
	file_selected.emit(_selected_file, _selected_meta)


func _on_file_item_activated(index: int) -> void:
	var e = _file_list.get_item_metadata(index)
	if typeof(e) != TYPE_DICTIONARY:
		return
	if bool(e.get("is_dir", false)):
		# Only navigable providers drill into folders; flat (server) directory
		# headers are non-selectable and do nothing when double-clicked.
		if _provider == null or _provider.supports_navigation():
			var folder_path := String(e.get("path", ""))
			if folder_path.is_empty():
				folder_path = _join(_current_dir, String(e.get("name", "")))
			_change_dir(folder_path, true)
		return
	_selected_file = String(e.get("path", ""))
	_selected_meta = e.get("meta", {})
	if _filename_edit:
		_filename_edit.text = String(e.get("name", ""))
	file_selected.emit(_selected_file, _selected_meta)


func _on_filter_option_selected(_idx: int) -> void:
	_rerender_cached()


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
	_rerender_cached()


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
