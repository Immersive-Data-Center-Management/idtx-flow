@tool
extends VBoxContainer

## Step 4 - Confirm Selection.
## Shows a 3-node step indicator (nodes 1 & 2 completed, node 3 active),
## a title, and the selected asset detail card. Confirm triggers the import.

signal confirmed
signal back_requested
signal cancel_requested

const WizardTheme    := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardFooter   := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const AssetPanel     := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")

var _asset_panel: Node


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", 16)


func _ready() -> void:
	_build()


func set_selected_path(file_path: String) -> void:
	if _asset_panel and _asset_panel.has_method("populate"):
		_asset_panel.populate(file_path)


func set_selected_meta(meta: Dictionary) -> void:
	if _asset_panel and _asset_panel.has_method("populate_from_dict"):
		_asset_panel.populate_from_dict(meta)


func _build() -> void:
	# Top area: step indicator + title -----------------------------------
	var top_panel := PanelContainer.new()
	top_panel.add_theme_stylebox_override("panel", WizardTheme.make_panel_style(WizardTheme.COLOR_PANEL))
	top_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_child(top_panel)

	var top_vb := VBoxContainer.new()
	top_vb.add_theme_constant_override("separation", 10)
	top_panel.add_child(top_vb)

	var indicator := _build_step_indicator()
	top_vb.add_child(indicator)

	var title := Label.new()
	title.text = "Confirm Selection"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT)
	title.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_HEADING))
	top_vb.add_child(title)

	var subtitle := Label.new()
	subtitle.text = "This will import the asset into view"
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle.add_theme_color_override("font_color", WizardTheme.COLOR_TEXT_CAPTION)
	subtitle.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_CAPTION))
	top_vb.add_child(subtitle)

	# Body: centered asset panel -----------------------------------------
	var body_center := CenterContainer.new()
	body_center.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body_center.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(body_center)

	_asset_panel = AssetPanel.new()
	(_asset_panel as Control).custom_minimum_size = Vector2(WizardTheme.px(360), 0)
	body_center.add_child(_asset_panel)
	if _asset_panel.has_method("set_header_style"):
		_asset_panel.set_header_style(1)  # SELECTED_ASSET

	# Footer -------------------------------------------------------------
	var footer := WizardFooter.new()
	add_child(footer)
	footer.setup(true, "Confirm", true)
	footer.back_pressed.connect(func(): back_requested.emit())
	footer.cancel_pressed.connect(func(): cancel_requested.emit())
	footer.primary_pressed.connect(func(): confirmed.emit())


func _build_step_indicator() -> Control:
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 0)
	row.custom_minimum_size = Vector2(0, WizardTheme.px(40))

	# Circle 1 (done, green)
	row.add_child(_make_circle(true, "✓", WizardTheme.COLOR_SUCCESS))
	row.add_child(_make_line(WizardTheme.COLOR_SUCCESS))
	# Circle 2 (done, green)
	row.add_child(_make_circle(true, "✓", WizardTheme.COLOR_SUCCESS))
	row.add_child(_make_line(WizardTheme.COLOR_SUCCESS))
	# Circle 3 (active, blue)
	row.add_child(_make_circle(false, "3", WizardTheme.COLOR_PRIMARY))

	return row


func _make_circle(is_check: bool, text: String, bg_color: Color) -> Control:
	var panel := Panel.new()
	var d := WizardTheme.px(28)
	panel.custom_minimum_size = Vector2(d, d)
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg_color
	var r := d / 2
	sb.corner_radius_top_left = r
	sb.corner_radius_top_right = r
	sb.corner_radius_bottom_left = r
	sb.corner_radius_bottom_right = r
	panel.add_theme_stylebox_override("panel", sb)

	var lbl := Label.new()
	lbl.text = text
	lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	lbl.add_theme_color_override("font_color", Color.WHITE)
	lbl.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	lbl.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.add_child(lbl)

	return panel


func _make_line(color: Color) -> Control:
	var line := Panel.new()
	line.custom_minimum_size = Vector2(WizardTheme.px(100), WizardTheme.px(2))
	line.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	var sb := StyleBoxFlat.new()
	sb.bg_color = color
	line.add_theme_stylebox_override("panel", sb)
	return line
