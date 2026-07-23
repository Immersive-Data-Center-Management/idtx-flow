@tool
extends VBoxContainer

## Self-contained asset details widget.
##
##   VBox (self)
##     ├─ Label "Asset Details"             (HeaderSmall variation)
##     └─ Control (fill, sized to expand)
##         ├─ ItemList (background, empty)   (ItemListSecondary, PRESET_FULL_RECT)
##         └─ ScrollContainer (overlay)      (PRESET_FULL_RECT)
##             └─ MarginContainer (padding)
##                 └─ VBox (content)
##                     ├─ Centered thumbnail (usd_file_vec.png)
##                     ├─ File Name  (HeaderSmall + value)
##                     ├─ File Path  (HeaderSmall + value, autowraps)
##                     └─ HBox
##                         ├─ File Size
##                         └─ Modified
##
## The widget owns its "Asset Details" header - the wizard's built-in header
## is replaced when this panel is plugged in via
## `WizardFileBrowser.set_side_panel(...)`. The widget can be reused in later
## wizard steps (like step 4 confirm) without external chrome.

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const _USD_ICON_PATH := "res://addons/IDTXFlow/import_manager/usd_file_vec.png"

var _bg_list: ItemList
var _thumb_icon: TextureRect
var _file_name_value: Label
var _file_path_value: Label
var _file_size_value: Label
var _modified_value: Label

var _current_path: String = ""


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL


func _ready() -> void:
	_ensure_built()
	if _thumb_icon and _thumb_icon.texture == null:
		_thumb_icon.texture = _load_usd_icon()


# ==========================================================================
# Public API
# ==========================================================================

func populate(file_path: String) -> void:
	_ensure_built()
	_current_path = file_path

	if file_path.is_empty():
		_file_name_value.text = "-"
		_file_path_value.text = "-"
		_file_size_value.text = "-"
		_modified_value.text = "-"
		return

	_file_name_value.text = file_path.get_file()
	_file_path_value.text = file_path

	var size_str := "-"
	var mtime_str := "-"

	if FileAccess.file_exists(file_path):
		var f := FileAccess.open(file_path, FileAccess.READ)
		if f:
			size_str = _format_size(f.get_length())
			f.close()
		var mtime := FileAccess.get_modified_time(file_path)
		if mtime > 0:
			mtime_str = Time.get_datetime_string_from_unix_time(mtime).substr(0, 10)

	_file_size_value.text = size_str
	_modified_value.text = mtime_str


func populate_from_dict(meta: Dictionary) -> void:
	_ensure_built()
	_current_path = String(meta.get("path", ""))
	if _current_path.is_empty():
		_file_name_value.text = String(meta.get("name", "-"))
		_file_path_value.text = "-"
	else:
		_file_name_value.text = String(meta.get("name", _current_path.get_file()))
		_file_path_value.text = _current_path

	var size_bytes := int(meta.get("size_bytes", 0))
	_file_size_value.text = _format_size(size_bytes) if size_bytes > 0 else "-"

	# `modified` may be a String (local/mock) or an int timestamp (server /files);
	# str() accepts any Variant, whereas String(<int>) is an invalid constructor call.
	var modified := str(meta.get("modified", ""))
	_modified_value.text = modified if not modified.is_empty() else "-"


# ==========================================================================
# Build
# ==========================================================================

func _ensure_built() -> void:
	if _file_name_value != null:
		return

	# Respect any larger minimum size a parent may have set (e.g. step_confirm
	# gives us a fixed card height), while guaranteeing sensible defaults so
	# the panel never collapses to just its header.
	custom_minimum_size = Vector2(
		max(custom_minimum_size.x, WizardTheme.px(150)),
		max(custom_minimum_size.y, WizardTheme.px(360))
	)

	# --- Header -------------------------------------------------------
	var header_label := Label.new()
	header_label.text = "Asset Details:"
	header_label.theme_type_variation = "HeaderSmall"
	add_child(header_label)

	# --- Container: ItemList background + content overlay -------------
	var container := Control.new()
	container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	container.size_flags_vertical = Control.SIZE_EXPAND_FILL
	# The container hosts only anchor-based (PRESET_FULL_RECT) children, which
	# contribute no minimum size. Without an explicit minimum height the whole
	# region collapses to zero and only the header label above is visible.
	container.custom_minimum_size = Vector2(0, WizardTheme.px(320))
	add_child(container)

	# Background: empty ItemList with ItemListSecondary variation
	_bg_list = ItemList.new()
	_bg_list.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_bg_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	_bg_list.theme_type_variation = "ItemListSecondary"
	container.add_child(_bg_list)

	# Overlay: ScrollContainer + margin + content VBox on top.
	var scroll := ScrollContainer.new()
	scroll.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.mouse_filter = Control.MOUSE_FILTER_PASS
	container.add_child(scroll)

	var margin := MarginContainer.new()
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var pad := WizardTheme.px(8)
	margin.add_theme_constant_override("margin_left", pad)
	margin.add_theme_constant_override("margin_right", pad)
	margin.add_theme_constant_override("margin_top", pad)
	margin.add_theme_constant_override("margin_bottom", pad)
	scroll.add_child(margin)

	var inner := VBoxContainer.new()
	inner.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	inner.add_theme_constant_override("separation", WizardTheme.px(6))
	margin.add_child(inner)

	_build_thumbnail(inner)

	_file_name_value = _add_meta_group(inner, "File Name")
	_file_path_value = _add_meta_group(inner, "File Path")
	_file_path_value.autowrap_mode = TextServer.AUTOWRAP_ARBITRARY
	_file_path_value.custom_minimum_size = Vector2(WizardTheme.px(1), 0)
	_file_path_value.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	var side_row := HBoxContainer.new()
	side_row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	side_row.add_theme_constant_override("separation", WizardTheme.px(8))
	inner.add_child(side_row)
	_file_size_value = _add_meta_group(side_row, "File Size")
	_modified_value = _add_meta_group(side_row, "Modified")


func _build_thumbnail(parent: VBoxContainer) -> void:
	var thumb_row := HBoxContainer.new()
	thumb_row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	thumb_row.alignment = BoxContainer.ALIGNMENT_CENTER
	parent.add_child(thumb_row)

	var thumb_panel := Panel.new()
	var s := WizardTheme.px(120)
	thumb_panel.custom_minimum_size = Vector2(s, s)
	thumb_panel.size_flags_horizontal = 0
	thumb_row.add_child(thumb_panel)

	_thumb_icon = TextureRect.new()
	_thumb_icon.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	# EXPAND_IGNORE_SIZE + STRETCH_KEEP_ASPECT: scales the icon down (or
	# up) to fit inside the 120×120 panel while preserving aspect ratio.
	_thumb_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_thumb_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_thumb_icon.texture = _load_usd_icon()
	thumb_panel.add_child(_thumb_icon)


func _add_meta_group(parent: Container, caption: String) -> Label:
	var group := VBoxContainer.new()
	group.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	group.add_theme_constant_override("separation", WizardTheme.px(2))
	parent.add_child(group)

	var header_label := Label.new()
	header_label.text = caption
	header_label.theme_type_variation = "HeaderSmall"
	group.add_child(header_label)

	var value_margin := MarginContainer.new()
	value_margin.add_theme_constant_override("margin_left", WizardTheme.px(8))
	value_margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	group.add_child(value_margin)

	var value := Label.new()
	value.text = "-"
	value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	value_margin.add_child(value)

	return value


# ==========================================================================
# Helpers
# ==========================================================================

func _load_usd_icon() -> Texture2D:
	# Check if Godot has successfully imported it as a resource yet.
	if ResourceLoader.exists(_USD_ICON_PATH):
		var res = load(_USD_ICON_PATH)
		if res is Texture2D:
			return res
			
	# Fallback: read the raw PNG from disk
	# first import pass (e.g. when the addon is loaded for the very first time
	# and the `.import` file hasn't been generated yet).
	var abs_path := ProjectSettings.globalize_path(_USD_ICON_PATH)
	if not FileAccess.file_exists(abs_path):
		return null
	var img := Image.new()
	if img.load(abs_path) == OK:
		return ImageTexture.create_from_image(img)
	return null


func _format_size(bytes: int) -> String:
	if bytes < 1024:
		return "%d B" % bytes
	if bytes < 1024 * 1024:
		return "%.1f KB" % (bytes / 1024.0)
	if bytes < 1024 * 1024 * 1024:
		return "%.1f MB" % (bytes / (1024.0 * 1024.0))
	return "%.2f GB" % (bytes / (1024.0 * 1024.0 * 1024.0))