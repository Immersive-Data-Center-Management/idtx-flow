@tool
extends VBoxContainer

## Step 1 - Select importer.
##
## Two entry points:
##   • Asset Server URL + Connect  -> shows an inline login panel.
##   • Import USD from local files -> goes straight to the local file browser.

signal local_files_requested
signal server_login_succeeded(url: String, username: String, remember: bool)
signal cancel_requested

const WizardTheme       := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader      := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter      := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const ServerLoginPanel  := preload("res://addons/IDTXFlow/import_manager/server_login_panel.gd")

const DEFAULT_URL := "https://aetherra-prime.aas-showroom.msp02.shoot.garden.example/usd"

# Shared visible height for the URL row (input + Connect) and the
# "Import USD from local files" button so all three align.
const ROW_HEIGHT := 44

var _url_input: LineEdit
var _connect_btn: Button
var _login_panel: Node


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(12))


func _ready() -> void:
	_build()


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(1, 4, "Select importer:", "Connect to the USD asset server or import from local files")

	# Body: a centered narrow column
	var body_center := CenterContainer.new()
	body_center.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body_center.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(body_center)

	var body := VBoxContainer.new()
	body.custom_minimum_size = Vector2(WizardTheme.px(460), 0)
	body.add_theme_constant_override("separation", WizardTheme.px(8))
	body_center.add_child(body)

	# --- Asset Server URL row ------------------------------------------
	var url_caption := Label.new()
	url_caption.text = "Asset Server URL"
	body.add_child(url_caption)

	var url_row := HBoxContainer.new()
	url_row.add_theme_constant_override("separation", WizardTheme.px(6))
	body.add_child(url_row)

	_url_input = LineEdit.new()
	_url_input.placeholder_text = "Input URL"
	_url_input.text = DEFAULT_URL
	_url_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_url_input.custom_minimum_size = Vector2(0, WizardTheme.px(ROW_HEIGHT))
	url_row.add_child(_url_input)

	_connect_btn = Button.new()
	_connect_btn.text = "Connect"
	_connect_btn.custom_minimum_size = Vector2(WizardTheme.px(80), WizardTheme.px(ROW_HEIGHT))
	_connect_btn.pressed.connect(_on_connect_pressed)
	url_row.add_child(_connect_btn)

	# --- "or" divider --------------------------------------------------
	var or_label := Label.new()
	or_label.text = "or"
	or_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	or_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_child(or_label)

	# --- Local files button --------------------------------------------
	var local_btn := Button.new()
	local_btn.text = "  Import USD from local files"
	local_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	local_btn.custom_minimum_size = Vector2(0, WizardTheme.px(ROW_HEIGHT))
	local_btn.pressed.connect(_on_local_pressed)
	body.add_child(local_btn)

	var icon := WizardTheme.get_editor_icon(self, "Load", "PackedScene")
	if icon:
		local_btn.icon = icon

	# --- Login panel (hidden until Connect is pressed) -----------------
	var sep := HSeparator.new()
	sep.visible = false
	body.add_child(sep)

	_login_panel = ServerLoginPanel.new()
	body.add_child(_login_panel)
	_login_panel.login_succeeded.connect(_on_login_succeeded)

	# Keep a reference to the separator so we can toggle it with the panel.
	_login_panel.set_meta("companion_separator", sep)

	# Bottom: pushes footer down
	var spacer := Control.new()
	spacer.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(spacer)

	# Footer: Cancel only
	var footer := WizardFooter.new()
	add_child(footer)
	footer.setup(false, "")
	footer.cancel_pressed.connect(_on_cancel_pressed)


func _on_connect_pressed() -> void:
	var url := _url_input.text.strip_edges()
	if url.is_empty():
		return
	# Reveal the login panel (and its top separator) below the "or / local"
	# section, still on this same step.
	if _login_panel:
		var sep = _login_panel.get_meta("companion_separator", null)
		if sep is HSeparator:
			sep.visible = true
		_login_panel.show_for_url(url)


func _on_login_succeeded(url: String, username: String, remember: bool) -> void:
	server_login_succeeded.emit(url, username, remember)


func _on_local_pressed() -> void:
	local_files_requested.emit()


func _on_cancel_pressed() -> void:
	cancel_requested.emit()