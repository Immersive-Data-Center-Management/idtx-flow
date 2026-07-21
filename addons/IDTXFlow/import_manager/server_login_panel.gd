@tool
extends VBoxContainer

## Inline "Asset Server requires login" panel.
##
## Shown on step 1 after the user clicks Connect on the URL row.
## On successful login (demo/demo), emits `login_succeeded`.

signal login_succeeded(url: String, username: String, remember: bool)

const WizardTheme    := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const ServerMockData := preload("res://addons/IDTXFlow/import_manager/server_mock_data.gd")

var _server_url: String = ""

var _username_input: LineEdit
var _password_input: LineEdit
var _error_panel: PanelContainer
var _error_label: Label
var _connect_btn: Button
var _remember_cb: CheckBox


func _init() -> void:
	visible = false
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(8))


func _ready() -> void:
	_build()


func show_for_url(url: String) -> void:
	_server_url = url
	_ensure_built()
	# Clear any previous error and password on re-open
	_hide_error()
	if _password_input:
		_password_input.text = ""
	visible = true


func hide_panel() -> void:
	visible = false
	_hide_error()


func _build() -> void:
	_ensure_built()


func _ensure_built() -> void:
	if _username_input != null:
		return

	var title := Label.new()
	title.text = "Asset Server requires login:"
	add_child(title)

	# Username --------------------------------------------------------
	var user_lbl := Label.new()
	user_lbl.text = "Username"
	add_child(user_lbl)

	_username_input = LineEdit.new()
	_username_input.placeholder_text = "Enter username"
	_username_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_username_input.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	add_child(_username_input)

	# Password --------------------------------------------------------
	var pw_lbl := Label.new()
	pw_lbl.text = "Password"
	add_child(pw_lbl)

	_password_input = LineEdit.new()
	_password_input.placeholder_text = "Enter password"
	_password_input.secret = true
	_password_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_password_input.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	_password_input.text_submitted.connect(func(_t): _try_login())
	add_child(_password_input)

	# Error banner (hidden until failed attempt) -----------------------
	_error_panel = PanelContainer.new()
	_error_panel.visible = false
	_error_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	var err_color := WizardTheme.get_error_color(self)
	var err_bg := err_color
	err_bg.a = 0.25
	var err_style := WizardTheme.make_solid_style(err_bg, 4)
	err_style.border_color = err_color
	err_style.border_width_left = 1
	err_style.border_width_top = 1
	err_style.border_width_right = 1
	err_style.border_width_bottom = 1
	err_style.content_margin_left = WizardTheme.px(10)
	err_style.content_margin_right = WizardTheme.px(10)
	err_style.content_margin_top = WizardTheme.px(8)
	err_style.content_margin_bottom = WizardTheme.px(8)
	_error_panel.add_theme_stylebox_override("panel", err_style)

	var err_row := HBoxContainer.new()
	err_row.add_theme_constant_override("separation", WizardTheme.px(8))
	_error_panel.add_child(err_row)

	var err_icon := Label.new()
	err_icon.text = "⊘"
	err_icon.add_theme_color_override("font_color", err_color)
	err_row.add_child(err_icon)

	_error_label = Label.new()
	_error_label.text = "Authentication failed. Invalid username or password."
	_error_label.add_theme_color_override("font_color", err_color)
	_error_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_error_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	err_row.add_child(_error_label)

	add_child(_error_panel)

	# Connect button --------------------------------------------------
	_connect_btn = Button.new()
	_connect_btn.text = "Connect"
	_connect_btn.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_connect_btn.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.BTN_HEIGHT + 4))
	_connect_btn.pressed.connect(_try_login)
	add_child(_connect_btn)

	# Remember credentials --------------------------------------------
	_remember_cb = CheckBox.new()
	_remember_cb.text = "Remember credentials"
	add_child(_remember_cb)


func _try_login() -> void:
	var user := _username_input.text
	var pw := _password_input.text
	if ServerMockData.get_credentials_valid(user, pw):
		_hide_error()
		login_succeeded.emit(_server_url, user, _remember_cb.button_pressed)
	else:
		_show_error("Authentication failed. Invalid username or password.")


func _show_error(msg: String) -> void:
	_error_label.text = msg
	_error_panel.visible = true


func _hide_error() -> void:
	_error_panel.visible = false