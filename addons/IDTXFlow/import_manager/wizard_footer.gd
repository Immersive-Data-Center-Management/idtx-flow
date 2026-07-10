@tool
extends VBoxContainer

## Reusable wizard footer.
##
## Contains an HSeparator on top, and a row of buttons:
##   [ Back ]   ...spacer...   [ Cancel ] [ Primary ]

signal back_pressed
signal cancel_pressed
signal primary_pressed

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")

var _back_btn: Button
var _cancel_btn: Button
var _primary_btn: Button
var _row: HBoxContainer


func _init() -> void:
	add_theme_constant_override("separation", WizardTheme.px(10))


func setup(show_back: bool, primary_label: String, primary_enabled: bool = true) -> void:
	_ensure_built()
	_back_btn.visible = show_back
	if primary_label == "":
		_primary_btn.visible = false
	else:
		_primary_btn.visible = true
		_primary_btn.text = primary_label
		_primary_btn.disabled = not primary_enabled


func set_primary_enabled(enabled: bool) -> void:
	if _primary_btn:
		_primary_btn.disabled = not enabled


func _ensure_built() -> void:
	if _row != null:
		return

	# Separator on top --------------------------------------------------
	var sep := HSeparator.new()
	var sep_style := WizardTheme.make_flat_style(WizardTheme.COLOR_BORDER, 0)
	sep.add_theme_stylebox_override("separator", sep_style)
	sep.add_theme_constant_override("separation", 1)
	add_child(sep)

	# Row --------------------------------------------------------------
	_row = HBoxContainer.new()
	_row.add_theme_constant_override("separation", WizardTheme.px(8))
	_row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_child(_row)

	var min_btn := Vector2(WizardTheme.px(72), WizardTheme.px(WizardTheme.BTN_HEIGHT))

	_back_btn = Button.new()
	_back_btn.text = "Back"
	_back_btn.custom_minimum_size = min_btn
	WizardTheme.apply_secondary_button(_back_btn)
	_back_btn.pressed.connect(_on_back)
	_row.add_child(_back_btn)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_row.add_child(spacer)

	_cancel_btn = Button.new()
	_cancel_btn.text = "Cancel"
	_cancel_btn.custom_minimum_size = min_btn
	WizardTheme.apply_secondary_button(_cancel_btn)
	_cancel_btn.pressed.connect(_on_cancel)
	_row.add_child(_cancel_btn)

	_primary_btn = Button.new()
	_primary_btn.text = "Next"
	_primary_btn.custom_minimum_size = min_btn
	WizardTheme.apply_primary_button(_primary_btn)
	_primary_btn.pressed.connect(_on_primary)
	_row.add_child(_primary_btn)


func _on_back() -> void:
	back_pressed.emit()


func _on_cancel() -> void:
	cancel_pressed.emit()


func _on_primary() -> void:
	primary_pressed.emit()
