@tool
extends Control

## All viewport-space "live collaboration session" visuals in one place:
##   - a colored border drawn around the 3D editor viewport (_draw), and
##   - a centered banner bar at the top  + the session id.
##
## Parented onto the editor's internal `Node3DEditorViewportContainer` (by
## indicators.gd), fills it, and is fully click-through so it never
## interferes with the viewport. The coordinator sets the color + session label and
## toggles visibility based on the current scene.

var border_color: Color = Color(0.24, 0.82, 0.36)
var border_width: float = 3.0

var _banner: PanelContainer = null
var _banner_label: Label = null   # second line: the session id


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	# Draw above the viewport contents.
	z_index = 4096
	_build_banner()


func _draw() -> void:
	# Unfilled rectangle inset by half the line width so the stroke stays inside.
	var half := border_width * 0.5
	var rect := Rect2(Vector2(half, half), size - Vector2(border_width, border_width))
	draw_rect(rect, border_color, false, border_width)


func _notification(what: int) -> void:
	# Repaint the border when resized so it tracks viewport size changes.
	if what == NOTIFICATION_RESIZED:
		queue_redraw()


## Set the session id shown on the banner's second line.
func set_session_label(text: String) -> void:
	if _banner_label != null:
		_banner_label.text = text


# ---------------------------------------------------------------------------
# Banner (centered, shrink-to-content bar at the top of the viewport)
# ---------------------------------------------------------------------------

func _build_banner() -> void:
	_banner = PanelContainer.new()
	_banner.name = "IDTXFlowSessionBanner"
	_banner.mouse_filter = Control.MOUSE_FILTER_IGNORE
	# Top-center, shrink to content (not full width).
	_banner.anchor_left = 0.5
	_banner.anchor_right = 0.5
	_banner.anchor_top = 0.0
	_banner.anchor_bottom = 0.0
	_banner.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_banner.grow_vertical = Control.GROW_DIRECTION_END
	_banner.offset_top = 4.0

	var style := StyleBoxFlat.new()
	var bg := border_color
	bg.a = 0.75   # mostly solid so the text is clearly legible
	style.bg_color = bg
	style.content_margin_left = 10.0
	style.content_margin_right = 10.0
	style.content_margin_top = 3.0
	style.content_margin_bottom = 3.0
	var radius := 4.0
	style.corner_radius_top_left = radius
	style.corner_radius_top_right = radius
	style.corner_radius_bottom_left = radius
	style.corner_radius_bottom_right = radius
	_banner.add_theme_stylebox_override("panel", style)

	var col := VBoxContainer.new()
	col.mouse_filter = Control.MOUSE_FILTER_IGNORE
	col.alignment = BoxContainer.ALIGNMENT_CENTER
	col.add_theme_constant_override("separation", 0)
	_banner.add_child(col)

	# Line 1: "● LIVE SESSION" — green dot, black text.
	var row := HBoxContainer.new()
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	col.add_child(row)

	var dot := Label.new()
	dot.text = "●"
	dot.add_theme_color_override("font_color", border_color)
	row.add_child(dot)

	var title := Label.new()
	title.text = "LIVE SESSION"
	title.add_theme_color_override("font_color", Color.BLACK)
	row.add_child(title)

	# Line 2: session id — black, same font size, centered.
	_banner_label = Label.new()
	_banner_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_banner_label.add_theme_color_override("font_color", Color.BLACK)
	col.add_child(_banner_label)

	add_child(_banner)
