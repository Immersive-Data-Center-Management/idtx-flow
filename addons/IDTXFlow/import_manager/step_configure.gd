@tool
extends VBoxContainer

## Step 3 - Configure import settings. Placeholder controls only.

signal next_requested
signal back_requested
signal cancel_requested

const WizardTheme  := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")

const CAPTION_WIDTH := 140


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(3, 4, "Configure:", "Define import settings")

	# Two-column layout: Prim Types on the left, Import Definition + Import
	# Settings stacked on the right.
	var cols := HBoxContainer.new()
	cols.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cols.add_theme_constant_override("separation", WizardTheme.px(12))
	add_child(cols)

	# Left column ------------------------------------------------------
	var left_col := VBoxContainer.new()
	left_col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left_col.add_theme_constant_override("separation", WizardTheme.px(6))
	cols.add_child(left_col)

	var prim_types := _make_section("Prim Types")
	left_col.add_child(prim_types)
	var prim_body := _get_section_content(prim_types)
	prim_body.add_child(_make_checkbox_row("A", true))
	prim_body.add_child(_make_checkbox_row("B", true))
	prim_body.add_child(_make_checkbox_row("C", true))
	prim_body.add_child(_make_checkbox_row("D", false))

	# Right column -----------------------------------------------------
	var right_col := VBoxContainer.new()
	right_col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right_col.add_theme_constant_override("separation", WizardTheme.px(6))
	cols.add_child(right_col)

	var import_def := _make_section("Import Definition")
	right_col.add_child(import_def)
	var def_body := _get_section_content(import_def)
	def_body.add_child(_make_checkbox_row("Cameras", true))
	def_body.add_child(_make_checkbox_row("Lights", true))

	var import_settings := _make_section("Import Settings")
	right_col.add_child(import_settings)
	var settings_body := _get_section_content(import_settings)
	settings_body.add_child(_make_spin_row("Scale", 1.0, 0.001, 1000.0, 0.001))
	settings_body.add_child(_make_spin_row("Light Intensity", 1.0, 0.0, 100.0, 0.1))

	# Push footer to the bottom.
	var spacer := Control.new()
	spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(spacer)

	# Footer
	var footer := WizardFooter.new()
	add_child(footer)
	footer.setup(true, "Next", true)
	footer.back_pressed.connect(func(): back_requested.emit())
	footer.cancel_pressed.connect(func(): cancel_requested.emit())
	footer.primary_pressed.connect(func(): next_requested.emit())


# ---------------------------------------------------------------------------
# Section helper
# ---------------------------------------------------------------------------

func _make_section(title: String) -> FoldableContainer:
	var fc := FoldableContainer.new()
	fc.title = title
	fc.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	fc.folded = false

	# Match the editor's configured corner radius
	var radius := WizardTheme.corner_radius()
	# Look up colors from `self` (already in tree); fc isn't added yet.
	var panel_bg := get_theme_color("dark_color_2", "Editor") if has_theme_color("dark_color_2", "Editor") else Color(0, 0, 0, 0.12)
	var title_bg := get_theme_color("dark_color_3", "Editor") if has_theme_color("dark_color_3", "Editor") else Color(0, 0, 0, 0.2)

	# Title bar (expanded): rounded TOP corners only, so it sits flush on the panel below.
	var title_sb := WizardTheme.make_solid_style(title_bg, 0)
	title_sb.corner_radius_top_left = radius
	title_sb.corner_radius_top_right = radius
	title_sb.content_margin_left = WizardTheme.px(8)
	title_sb.content_margin_right = WizardTheme.px(8)
	title_sb.content_margin_top = WizardTheme.px(4)
	title_sb.content_margin_bottom = WizardTheme.px(4)
	fc.add_theme_stylebox_override("title_panel", title_sb)

	# Title bar (collapsed / folded): all four corners rounded since no panel shows.
	var title_collapsed_sb := title_sb.duplicate()
	title_collapsed_sb.corner_radius_bottom_left = radius
	title_collapsed_sb.corner_radius_bottom_right = radius
	fc.add_theme_stylebox_override("title_collapsed_panel", title_collapsed_sb)

	# Content panel: rounded BOTTOM corners only.
	var panel_sb := WizardTheme.make_solid_style(panel_bg, 0)
	panel_sb.corner_radius_bottom_left = radius
	panel_sb.corner_radius_bottom_right = radius
	panel_sb.content_margin_left = WizardTheme.px(8)
	panel_sb.content_margin_right = WizardTheme.px(8)
	panel_sb.content_margin_top = WizardTheme.px(6)
	panel_sb.content_margin_bottom = WizardTheme.px(6)
	fc.add_theme_stylebox_override("panel", panel_sb)

	var content := VBoxContainer.new()
	content.name = "Content"
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	content.add_theme_constant_override("separation", WizardTheme.px(4))
	fc.add_child(content)

	return fc


func _get_section_content(section: FoldableContainer) -> VBoxContainer:
	return section.get_node("Content") as VBoxContainer


# ---------------------------------------------------------------------------
# Property row helpers
# ---------------------------------------------------------------------------

# Left-caption + right-editor row layout
func _make_property_row(caption: String, editor: Control) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_theme_constant_override("separation", WizardTheme.px(8))

	var lbl := Label.new()
	lbl.text = caption
	# SIZE_FILL (without EXPAND) so the caption stays at its fixed-width column
	# and does not steal horizontal space from the editor on its right.
	lbl.size_flags_horizontal = Control.SIZE_FILL
	lbl.custom_minimum_size = Vector2(WizardTheme.px(CAPTION_WIDTH), 0)
	row.add_child(lbl)

	editor.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(editor)

	return row


func _make_checkbox_row(caption: String, checked: bool) -> HBoxContainer:
	var cb := CheckBox.new()
	cb.button_pressed = checked
	return _make_property_row(caption, cb)


func _make_spin_row(caption: String, value: float, min_v: float, max_v: float, step: float) -> HBoxContainer:
	var sb := SpinBox.new()
	sb.min_value = min_v
	sb.max_value = max_v
	sb.step = step
	sb.value = value
	if sb.has_method("set_use_rounded_values"):
		sb.set_use_rounded_values(false)
	sb.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	return _make_property_row(caption, sb)