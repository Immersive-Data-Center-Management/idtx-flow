@tool
extends RefCounted

## Shared styling helpers used by all wizard components.
##
## Provides color constants and StyleBox factories so every step of the
## USD Import Manager has a consistent look. Colors are picked to match
## the mockup screenshots as closely as possible using only Godot built-in
## UI primitives (no external assets required).

# Base palette ----------------------------------------------------------------
const COLOR_BG            := Color(0.13, 0.14, 0.16)          # overall background
const COLOR_PANEL         := Color(0.16, 0.18, 0.21)          # panel / card
const COLOR_PANEL_ALT     := Color(0.18, 0.20, 0.24)          # inputs, inner cards
const COLOR_BORDER        := Color(0.22, 0.25, 0.30)          # subtle borders / separators
const COLOR_PRIMARY       := Color(0.24, 0.48, 0.72)          # blue accent
const COLOR_PRIMARY_HOVER := Color(0.29, 0.55, 0.82)
const COLOR_SUCCESS       := Color(0.15, 0.65, 0.40)          # green (checkmarks / done)
const COLOR_TEXT          := Color(0.85, 0.87, 0.90)
const COLOR_TEXT_MUTED    := Color(0.62, 0.65, 0.70)
const COLOR_TEXT_CAPTION  := Color(0.52, 0.56, 0.62)
const COLOR_SELECTION     := Color(0.22, 0.30, 0.42)


# Font sizes (logical / unscaled) ---------------------------------------------
const FONT_SIZE_TITLE   := 16   # window title "USD Importer"
const FONT_SIZE_HEADING := 15   # step title / section headings
const FONT_SIZE_BODY    := 13   # normal body text, list items, buttons, values
const FONT_SIZE_CAPTION := 11   # small captions above inputs
const FONT_SIZE_SMALL   := 10

# Common sizing (logical / unscaled) ------------------------------------------
const BTN_HEIGHT        := 28
const INPUT_HEIGHT      := 26
const NAV_BTN_SIZE      := 26

# Editor scale (set by import_manager.gd from EditorInterface.get_editor_scale()).
# All fs()/px() calls multiply by this so we match the editor's own scaling.
static var editor_scale: float = 1.0


static func fs(base: int) -> int:
	# Scale a font size by the editor scale factor.
	return int(round(float(base) * editor_scale))


static func px(base: int) -> int:
	# Scale a pixel dimension by the editor scale factor.
	return int(round(float(base) * editor_scale))


# StyleBox factories ----------------------------------------------------------

static func make_panel_style(bg: Color = COLOR_PANEL, border: Color = COLOR_BORDER, radius: int = 4) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.border_color = border
	sb.border_width_left = 1
	sb.border_width_top = 1
	sb.border_width_right = 1
	sb.border_width_bottom = 1
	sb.corner_radius_top_left = radius
	sb.corner_radius_top_right = radius
	sb.corner_radius_bottom_left = radius
	sb.corner_radius_bottom_right = radius
	sb.content_margin_left = 12
	sb.content_margin_top = 10
	sb.content_margin_right = 12
	sb.content_margin_bottom = 10
	return sb


static func make_flat_style(bg: Color, radius: int = 4) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.corner_radius_top_left = radius
	sb.corner_radius_top_right = radius
	sb.corner_radius_bottom_left = radius
	sb.corner_radius_bottom_right = radius
	return sb


static func make_input_style(alpha: float = 1.0) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	var bg := COLOR_PANEL_ALT
	bg.a = alpha
	sb.bg_color = bg
	var border := COLOR_BORDER
	border.a = alpha
	sb.border_color = border
	sb.border_width_left = 1
	sb.border_width_top = 1
	sb.border_width_right = 1
	sb.border_width_bottom = 1
	sb.corner_radius_top_left = 3
	sb.corner_radius_top_right = 3
	sb.corner_radius_bottom_left = 3
	sb.corner_radius_bottom_right = 3
	sb.content_margin_left = px(8)
	sb.content_margin_top = px(5)
	sb.content_margin_right = px(8)
	sb.content_margin_bottom = px(5)
	return sb


static func _btn_style(bg: Color, radius: int, hpad: int, vpad: int) -> StyleBoxFlat:
	var sb := make_flat_style(bg, radius)
	sb.content_margin_left = hpad
	sb.content_margin_right = hpad
	sb.content_margin_top = vpad
	sb.content_margin_bottom = vpad
	return sb


static func apply_primary_button(btn: Button) -> void:
	var hp := px(14)
	var vp := px(7)
	btn.add_theme_stylebox_override("normal", _btn_style(COLOR_PRIMARY, 3, hp, vp))
	btn.add_theme_stylebox_override("hover", _btn_style(COLOR_PRIMARY_HOVER, 3, hp, vp))
	btn.add_theme_stylebox_override("pressed", _btn_style(COLOR_PRIMARY.darkened(0.1), 3, hp, vp))
	btn.add_theme_stylebox_override("disabled", _btn_style(Color(COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 0.4), 3, hp, vp))
	btn.add_theme_color_override("font_color", Color.WHITE)
	btn.add_theme_color_override("font_hover_color", Color.WHITE)
	btn.add_theme_color_override("font_pressed_color", Color.WHITE)
	btn.add_theme_color_override("font_disabled_color", Color(1, 1, 1, 0.5))
	btn.add_theme_font_size_override("font_size", fs(FONT_SIZE_BODY))


static func apply_secondary_button(btn: Button) -> void:
	var hp := px(14)
	var vp := px(7)
	btn.add_theme_stylebox_override("normal", _btn_style(Color(0.20, 0.22, 0.26), 3, hp, vp))
	btn.add_theme_stylebox_override("hover", _btn_style(Color(0.24, 0.27, 0.32), 3, hp, vp))
	btn.add_theme_stylebox_override("pressed", _btn_style(Color(0.18, 0.20, 0.24), 3, hp, vp))
	btn.add_theme_stylebox_override("disabled", _btn_style(Color(0.20, 0.22, 0.26, 0.5), 3, hp, vp))
	btn.add_theme_color_override("font_color", COLOR_TEXT)
	btn.add_theme_color_override("font_hover_color", Color.WHITE)
	btn.add_theme_color_override("font_disabled_color", Color(COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b, 0.4))
	btn.add_theme_font_size_override("font_size", fs(FONT_SIZE_BODY))


static func apply_ghost_button(btn: Button) -> void:
	# Big full-width "Import USD from local files" style: dark panel look.
	var hp := px(14)
	var vp := px(10)
	btn.add_theme_stylebox_override("normal", _btn_style(Color(0.20, 0.22, 0.26), 4, hp, vp))
	btn.add_theme_stylebox_override("hover", _btn_style(Color(0.24, 0.27, 0.32), 4, hp, vp))
	btn.add_theme_stylebox_override("pressed", _btn_style(Color(0.18, 0.20, 0.24), 4, hp, vp))
	btn.add_theme_color_override("font_color", COLOR_TEXT)
	btn.add_theme_color_override("font_hover_color", Color.WHITE)
	btn.add_theme_font_size_override("font_size", fs(FONT_SIZE_BODY))


static func apply_line_edit_style(le: LineEdit, disabled: bool = false) -> void:
	var alpha := 0.4 if disabled else 1.0
	le.add_theme_stylebox_override("normal", make_input_style(alpha))
	le.add_theme_stylebox_override("focus", make_input_style(alpha))
	le.add_theme_stylebox_override("read_only", make_input_style(alpha))
	var text_color := COLOR_TEXT
	text_color.a = alpha
	le.add_theme_color_override("font_color", text_color)
	le.add_theme_color_override("font_placeholder_color", COLOR_TEXT_CAPTION)
	le.add_theme_font_size_override("font_size", fs(FONT_SIZE_BODY))


static func get_editor_icon(host: Control, icon_name: String, fallback: String = "") -> Texture2D:
	# Try to fetch a Godot EditorIcons theme icon via the host Control's
	# theme lookup (this resolves via the editor theme when the control is
	# inside the editor tree).
	if host and host.is_inside_tree():
		if host.has_theme_icon(icon_name, "EditorIcons"):
			return host.get_theme_icon(icon_name, "EditorIcons")
		if fallback != "" and host.has_theme_icon(fallback, "EditorIcons"):
			return host.get_theme_icon(fallback, "EditorIcons")
	return null
