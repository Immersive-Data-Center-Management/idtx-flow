@tool
extends VBoxContainer

## Step 4 - Confirm Selection.
## Header (Step 4 of 4) + Import-destination radios + AssetDetailPanel + footer.

signal confirm_requested
signal back_requested
signal cancel_requested
signal destination_changed(destination: String)

const WizardTheme    := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader   := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter   := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const AssetPanel     := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")

# Destination string identifiers.
const DEST_CURRENT := "current"
const DEST_NEW     := "new"

var _asset_panel: Node

var _radio_current: CheckBox
var _radio_new: CheckBox
var _target_info_label: Label


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()


func set_selected_path(file_path: String) -> void:
	if _asset_panel and _asset_panel.has_method("populate"):
		_asset_panel.populate(file_path)


func set_selected_meta(meta: Dictionary) -> void:
	if _asset_panel and _asset_panel.has_method("populate_from_dict"):
		_asset_panel.populate_from_dict(meta)


## Updates the "Target:" info line beneath the "Import into current scene"
## radio. When `enabled` is false the current-scene option is greyed out and
## the "new scene" option is auto-selected.
func set_current_target_info(display_name: String, sub_text: String, enabled: bool) -> void:
	if _target_info_label:
		if enabled:
			_target_info_label.text = "Target: %s  (%s)" % [display_name, sub_text]
		else:
			_target_info_label.text = "No scene open. Open a scene to use this option."

	if _radio_current:
		_radio_current.disabled = not enabled
		if not enabled and _radio_current.button_pressed:
			# Fall back to "new scene" so we don't leave a disabled radio selected.
			_radio_new.button_pressed = true


func get_destination() -> String:
	if _radio_new and _radio_new.button_pressed:
		return DEST_NEW
	return DEST_CURRENT


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(4, 4, "Confirm:", "Review and import selected asset")

	# Import destination section
	add_child(_build_destination_section())

	# Selected asset card, centered
	var body_center := CenterContainer.new()
	body_center.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body_center.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(body_center)

	_asset_panel = AssetPanel.new()
	# Give the panel a real minimum height - CenterContainer sizes its child
	# to the child's minimum size, and the panel's content region is built
	# from anchor-based (PRESET_FULL_RECT) children that contribute no
	# minimum height. Without this, only the "Asset Details:" header shows.
	(_asset_panel as Control).custom_minimum_size = Vector2(WizardTheme.px(360), WizardTheme.px(420))
	body_center.add_child(_asset_panel)
	if _asset_panel.has_method("set_header_style"):
		_asset_panel.set_header_style(1)  # SELECTED_ASSET

	var footer := WizardFooter.new()
	add_child(footer)
	footer.setup(true, "Import", true)
	footer.back_pressed.connect(func(): back_requested.emit())
	footer.cancel_pressed.connect(func(): cancel_requested.emit())
	footer.primary_pressed.connect(func(): confirm_requested.emit())


func _build_destination_section() -> Control:
	var section := VBoxContainer.new()
	section.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	section.add_theme_constant_override("separation", WizardTheme.px(4))

	var heading := Label.new()
	heading.text = "Import destination"
	heading.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_HEADING))
	section.add_child(heading)

	# CheckBox controls in a shared ButtonGroup behave as mutually exclusive
	# radio buttons.
	var group := ButtonGroup.new()

	_radio_current = CheckBox.new()
	_radio_current.text = "Import into current scene"
	_radio_current.button_group = group
	_radio_current.button_pressed = true
	_radio_current.toggled.connect(_on_radio_current_toggled)
	section.add_child(_radio_current)

	_target_info_label = Label.new()
	_target_info_label.text = "Target: -"
	_target_info_label.add_theme_color_override("font_color", WizardTheme.get_muted_color(self))
	_target_info_label.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_CAPTION))
	# Slight indent so it reads as a child of the radio above.
	var indent := HBoxContainer.new()
	indent.add_theme_constant_override("separation", 0)
	var spacer_l := Control.new()
	spacer_l.custom_minimum_size = Vector2(WizardTheme.px(24), 0)
	indent.add_child(spacer_l)
	indent.add_child(_target_info_label)
	section.add_child(indent)

	_radio_new = CheckBox.new()
	_radio_new.text = "Import into new scene"
	_radio_new.button_group = group
	_radio_new.toggled.connect(_on_radio_new_toggled)
	section.add_child(_radio_new)

	var new_info := Label.new()
	new_info.text = "A new scene will be created with the imported USD stage as its root."
	new_info.add_theme_color_override("font_color", WizardTheme.get_muted_color(self))
	new_info.add_theme_font_size_override("font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_CAPTION))
	var indent2 := HBoxContainer.new()
	indent2.add_theme_constant_override("separation", 0)
	var spacer_l2 := Control.new()
	spacer_l2.custom_minimum_size = Vector2(WizardTheme.px(24), 0)
	indent2.add_child(spacer_l2)
	indent2.add_child(new_info)
	section.add_child(indent2)

	return section


func _on_radio_current_toggled(pressed: bool) -> void:
	if pressed:
		destination_changed.emit(DEST_CURRENT)


func _on_radio_new_toggled(pressed: bool) -> void:
	if pressed:
		destination_changed.emit(DEST_NEW)