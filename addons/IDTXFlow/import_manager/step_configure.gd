@tool
extends VBoxContainer

## Step 3 - Configure import settings. Placeholder controls only.

signal import_requested
signal back_requested
signal cancel_requested

const WizardTheme  := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(3, 3, "Configure:", "Define import settings")

	var cols := HBoxContainer.new()
	cols.add_theme_constant_override("separation", WizardTheme.px(10))
	cols.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cols.size_flags_vertical = Control.SIZE_FILL
	add_child(cols)

	# Left column: Prim Types
	var left := _make_section("Prim Types")
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cols.add_child(left)

	var prim_body := _get_section_body(left)
	var include_row := HBoxContainer.new()
	include_row.add_theme_constant_override("separation", 16)
	prim_body.add_child(include_row)

	var include_lbl := Label.new()
	include_lbl.text = "Include"
	include_lbl.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT_MUTED)
	include_lbl.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	include_row.add_child(include_lbl)

	var options := VBoxContainer.new()
	options.add_theme_constant_override("separation", 4)
	include_row.add_child(options)

	options.add_child(_make_checkbox("A", true))
	options.add_child(_make_checkbox("C", true))
	options.add_child(_make_checkbox("C", true))
	options.add_child(_make_checkbox("D", false))

	# Right column: two stacked sections
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 12)
	cols.add_child(right)

	var import_def := _make_section("Import Definition")
	right.add_child(import_def)
	var def_body := _get_section_body(import_def)
	def_body.add_child(_make_checkbox("Cameras", true))
	def_body.add_child(_make_checkbox("Lights", true))

	var import_settings := _make_section("Import Settings")
	right.add_child(import_settings)
	var settings_body := _get_section_body(import_settings)
	settings_body.add_child(_make_setting_row("Scale", "1.000"))
	settings_body.add_child(_make_setting_row("Light Intensity …", "1.000"))

	# Spacer
	var spacer := Control.new()
	spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(spacer)

	# Footer
	var footer := WizardFooter.new()
	add_child(footer)
	footer.setup(true, "Import", true)
	footer.back_pressed.connect(func(): back_requested.emit())
	footer.cancel_pressed.connect(func(): cancel_requested.emit())
	footer.primary_pressed.connect(func(): import_requested.emit())


func _make_section(title: String) -> PanelContainer:
	var pc := PanelContainer.new()
	pc.add_theme_stylebox_override("panel", WizardTheme.make_panel_style(WizardTheme.COLOR_PANEL))
	pc.size_flags_vertical = Control.SIZE_FILL

	var vb := VBoxContainer.new()
	vb.name = "SectionRoot"
	vb.add_theme_constant_override("separation", 8)
	pc.add_child(vb)

	# Header with chevron (visual only)
	var header_row := HBoxContainer.new()
	header_row.add_theme_constant_override("separation", 4)
	vb.add_child(header_row)

	var chevron := Label.new()
	chevron.text = "▾"
	chevron.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT_MUTED)
	chevron.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	header_row.add_child(chevron)

	var title_lbl := Label.new()
	title_lbl.text = title
	title_lbl.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	title_lbl.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	header_row.add_child(title_lbl)

	# Body container
	var body := VBoxContainer.new()
	body.name = "SectionBody"
	body.add_theme_constant_override("separation", 6)
	vb.add_child(body)

	return pc


func _get_section_body(section: PanelContainer) -> VBoxContainer:
	var root := section.get_child(0) as VBoxContainer
	return root.get_node("SectionBody") as VBoxContainer


func _make_checkbox(text: String, checked: bool) -> CheckBox:
	var cb := CheckBox.new()
	cb.text = text
	cb.button_pressed = checked
	cb.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	cb.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	return cb


func _make_setting_row(caption: String, value: String) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)

	var lbl := Label.new()
	lbl.text = caption
	lbl.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT_MUTED)
	lbl.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	lbl.custom_minimum_size = Vector2(WizardTheme.px(140), 0)
	row.add_child(lbl)

	var le := LineEdit.new()
	le.text = value
	le.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	le.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	WizardTheme.apply_line_edit_style(le)
	row.add_child(le)

	return row
