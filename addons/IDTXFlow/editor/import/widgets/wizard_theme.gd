@tool
extends RefCounted

## Thin helper module for the USD Import Manager wizard.
##
## The wizard is editor-only and inherits the vast majority of its look
## from Godot's editor theme (Editor Settings → Interface → Theme). This
## module therefore keeps only:
##
##   • Layout constants (button/input heights, nav-button size) — these
##     are not styling, just geometry, and are scaled by the editor's UI
##     zoom via `px()` / `fs()`.
##
##   • A small set of *semantic* color accessors that read from the
##     editor theme (accent, success, error, muted text). We use these
##     in the very few places where we compose custom shapes that carry
##     meaning (the step-4 indicator circles, the ✓ marks, the error
##     banner in the login panel).
##
##   • Editor icon lookup (`get_editor_icon`).
##
## Everything else (Button/LineEdit/PanelContainer/ProgressBar/ItemList/
## SpinBox/CheckBox/HSeparator styling) is left to the editor theme.

# Font sizes (logical / unscaled) --------------------------------------------
const FONT_SIZE_TITLE   := 16
const FONT_SIZE_HEADING := 15
const FONT_SIZE_BODY    := 13
const FONT_SIZE_CAPTION := 11
const FONT_SIZE_SMALL   := 10

# Common sizing (logical / unscaled) -----------------------------------------
const BTN_HEIGHT        := 28
const INPUT_HEIGHT      := 26
const NAV_BTN_SIZE      := 26

# Editor scale (set by import_manager.gd from EditorInterface.get_editor_scale()).
static var editor_scale: float = 1.0


static func fs(base: int) -> int:
	return int(round(float(base) * editor_scale))


static func px(base: int) -> int:
	return int(round(float(base) * editor_scale))


# Corner radius as configured in the editor (Editor Settings → Interface →
# Theme → Corner Radius), scaled by the editor UI zoom. Falls back to a sane
# default when EditorSettings is unavailable (e.g. running outside the editor).
static func corner_radius(default_base: int = 6) -> int:
	var base := default_base
	if Engine.is_editor_hint():
		var es := EditorInterface.get_editor_settings()
		if es and es.has_setting("interface/theme/corner_radius"):
			base = int(es.get_setting("interface/theme/corner_radius"))
	return px(base)


# ---------------------------------------------------------------------------
# Editor theme accessors
# ---------------------------------------------------------------------------
# All lookups go via the given `host` Control's theme chain so we pick up the
# editor theme automatically (the host must be inside the editor tree, which
# is true for every control the wizard builds).

static func get_accent_color(host: Control) -> Color:
	return host.get_theme_color("accent_color", "Editor")


static func get_success_color(host: Control) -> Color:
	return host.get_theme_color("success_color", "Editor")


static func get_error_color(host: Control) -> Color:
	return host.get_theme_color("error_color", "Editor")


static func get_muted_color(host: Control) -> Color:
	return host.get_theme_color("font_disabled_color", "Label")


# The inspector tints its collapsible sub-sections (Transform / Visibility / …)
# using the theme's `prop_subsection` color drawn at a LOW ALPHA over whatever
# is behind — it is NOT a different hue and NOT an opaque fill. This returns
# that exact base color (with a faint neutral fallback); callers apply the
# alpha reduction the inspector uses (roughly `a *= 0.4`).
static func get_subsection_color(host: Control) -> Color:
	if host.has_theme_color("prop_subsection", "Editor"):
		return host.get_theme_color("prop_subsection", "Editor")
	# Faint neutral fallback (already semi-transparent) so callers get a subtle
	# tint even when the editor color is unavailable.
	return Color(1, 1, 1, 0.06)


static func get_editor_icon(host: Control, icon_name: String, fallback: String = "") -> Texture2D:
	if host and host.is_inside_tree():
		if host.has_theme_icon(icon_name, "EditorIcons"):
			return host.get_theme_icon(icon_name, "EditorIcons")
		if fallback != "" and host.has_theme_icon(fallback, "EditorIcons"):
			return host.get_theme_icon(fallback, "EditorIcons")
	return null


# ---------------------------------------------------------------------------
# Ad-hoc stylebox factories (used only where we must compose custom shapes)
# ---------------------------------------------------------------------------

static func make_solid_style(bg: Color, corner_radius: int = 0) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.corner_radius_top_left = corner_radius
	sb.corner_radius_top_right = corner_radius
	sb.corner_radius_bottom_left = corner_radius
	sb.corner_radius_bottom_right = corner_radius
	return sb