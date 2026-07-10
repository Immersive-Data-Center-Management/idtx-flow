@tool
extends PanelContainer

## Reusable "Asset Details" / "Selected Asset" card.
##
## Shows a small green check header, a placeholder thumbnail area, and
## a metadata block (File Name, File Size, Modified).

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")

enum HeaderStyle {
	ASSET_DETAILS,
	SELECTED_ASSET,
}

var _title_label: Label
var _thumb_panel: Panel
var _thumb_icon: TextureRect
var _file_name_value: Label
var _file_size_value: Label
var _modified_value: Label

var _current_path: String = ""


func _init() -> void:
	add_theme_stylebox_override("panel", WizardTheme.make_panel_style(WizardTheme.COLOR_PANEL))


func _ready() -> void:
	_ensure_built()
	if _thumb_icon and _thumb_icon.texture == null:
		var tex := WizardTheme.get_editor_icon(self, "ImageTexture", "PackedScene")
		if tex:
			_thumb_icon.texture = tex


func set_header_style(style: int) -> void:
	_ensure_built()
	match style:
		HeaderStyle.SELECTED_ASSET:
			_title_label.text = "Selected Asset"
		_:
			_title_label.text = "Asset Details"


func populate(file_path: String) -> void:
	_ensure_built()
	_current_path = file_path

	_file_name_value.text = file_path.get_file()

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


# Populate directly from a server-side metadata dict (no FileAccess used).
# Expected keys: "name", "path", "size_bytes", "modified".
func populate_from_dict(meta: Dictionary) -> void:
	_ensure_built()
	_current_path = String(meta.get("path", ""))
	_file_name_value.text = String(meta.get("name", _current_path.get_file()))

	var size_bytes := int(meta.get("size_bytes", 0))
	_file_size_value.text = _format_size(size_bytes) if size_bytes > 0 else "-"

	var modified := String(meta.get("modified", ""))
	_modified_value.text = modified if not modified.is_empty() else "-"


func _format_size(bytes: int) -> String:
	if bytes < 1024:
		return "%d B" % bytes
	if bytes < 1024 * 1024:
		return "%.1f KB" % (bytes / 1024.0)
	if bytes < 1024 * 1024 * 1024:
		return "%.1f MB" % (bytes / (1024.0 * 1024.0))
	return "%.2f GB" % (bytes / (1024.0 * 1024.0 * 1024.0))


func _ensure_built() -> void:
	if _file_name_value != null:
		return

	custom_minimum_size = Vector2(WizardTheme.px(240), 0)

	var root := VBoxContainer.new()
	root.add_theme_constant_override("separation", WizardTheme.px(10))
	add_child(root)

	# Header row (green check + title)
	var header := HBoxContainer.new()
	header.add_theme_constant_override("separation", WizardTheme.px(6))
	root.add_child(header)

	var check := Label.new()
	check.text = "✓"
	check.add_theme_color_override("font_color", WizardTheme.COLOR_SUCCESS)
	check.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_HEADING))
	header.add_child(check)

	_title_label = Label.new()
	_title_label.text = "Asset Details"
	_title_label.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	_title_label.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	header.add_child(_title_label)

	# Thumbnail placeholder ---------------------------------------------
	_thumb_panel = Panel.new()
	_thumb_panel.custom_minimum_size = Vector2(0, WizardTheme.px(120))
	_thumb_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_thumb_panel.add_theme_stylebox_override("panel", WizardTheme.make_panel_style(WizardTheme.COLOR_PANEL_ALT))
	root.add_child(_thumb_panel)

	_thumb_icon = TextureRect.new()
	_thumb_icon.expand_mode = TextureRect.EXPAND_KEEP_SIZE
	_thumb_icon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	_thumb_icon.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_thumb_icon.modulate = Color(1, 1, 1, 0.35)
	_thumb_panel.add_child(_thumb_icon)

	# Metadata block ----------------------------------------------------
	_file_name_value = _add_meta_row(root, "File Name", "-")

	var grid_row := HBoxContainer.new()
	grid_row.add_theme_constant_override("separation", WizardTheme.px(24))
	root.add_child(grid_row)

	_file_size_value = _add_meta_column(grid_row, "File Size", "-")
	_modified_value = _add_meta_column(grid_row, "Modified", "-")


func _add_meta_row(parent: Container, caption: String, value: String) -> Label:
	var col := VBoxContainer.new()
	col.add_theme_constant_override("separation", WizardTheme.px(2))
	parent.add_child(col)

	var cap := Label.new()
	cap.text = caption
	cap.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT_CAPTION)
	cap.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_CAPTION))
	col.add_child(cap)

	var val := Label.new()
	val.text = value
	val.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	val.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	col.add_child(val)

	return val


func _add_meta_column(parent: Container, caption: String, value: String) -> Label:
	var col := VBoxContainer.new()
	col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	col.add_theme_constant_override("separation", WizardTheme.px(2))
	parent.add_child(col)

	var cap := Label.new()
	cap.text = caption
	cap.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT_CAPTION)
	cap.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_CAPTION))
	col.add_child(cap)

	var val := Label.new()
	val.text = value
	val.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	val.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	col.add_child(val)

	return val
