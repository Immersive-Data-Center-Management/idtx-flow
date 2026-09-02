@tool
extends VBoxContainer

## Step 1 - Select importer.
##
## Two entry points:
##   • Import USD from local files -> goes straight to the local file browser.
##   • Asset Server URL + Connect  -> shows an inline login panel.

signal local_files_requested
signal server_login_succeeded(url: String, username: String, remember: bool)
signal cancel_requested

const WizardTheme       := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader      := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter      := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const ServerLoginPanel  := preload("res://addons/IDTXFlow/import_manager/server_login_panel.gd")
const IdtxAccess        := preload("res://addons/IDTXFlow/import_manager/idtx_client_access.gd")

const DEFAULT_URL := "http://localhost:8080"

# Shared visible height for the URL row (input + Connect) and the
# "Import USD from local files" button so all three align.
const ROW_HEIGHT := 44

var _url_input: LineEdit
var _connect_btn: Button
var _login_panel: Node

# Inline health-check status banner (hidden until a failed reachability probe).
var _status_panel: PanelContainer
var _status_label: Label
var _status_icon: Label

var _default_url := DEFAULT_URL

func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(12))


func _ready() -> void:
	_build()


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(1, 3, "Select importer:", "Import from local files or connect to the USD asset server")

	# Body: a centered narrow column
	var body_center := CenterContainer.new()
	body_center.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body_center.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(body_center)

	var body := VBoxContainer.new()
	body.custom_minimum_size = Vector2(WizardTheme.px(460), 0)
	body.add_theme_constant_override("separation", WizardTheme.px(8))
	body_center.add_child(body)

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

	# --- "or" divider --------------------------------------------------
	var or_label := Label.new()
	or_label.text = "or"
	or_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	or_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_child(or_label)

	# --- Asset Server URL row ------------------------------------------
	var url_caption := Label.new()
	url_caption.text = "Asset Server URL"
	body.add_child(url_caption)

	var url_row := HBoxContainer.new()
	url_row.add_theme_constant_override("separation", WizardTheme.px(6))
	body.add_child(url_row)

	_url_input = LineEdit.new()
	_url_input.placeholder_text = "Input URL"
	_url_input.text = _default_url if !_default_url.is_empty() else DEFAULT_URL
	_url_input.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_url_input.custom_minimum_size = Vector2(0, WizardTheme.px(ROW_HEIGHT))
	url_row.add_child(_url_input)

	_connect_btn = Button.new()
	_connect_btn.text = "Connect"
	_connect_btn.custom_minimum_size = Vector2(WizardTheme.px(80), WizardTheme.px(ROW_HEIGHT))
	_connect_btn.pressed.connect(_on_connect_pressed)
	url_row.add_child(_connect_btn)

	# --- Health-check status banner (hidden until a failed probe) -------
	_build_status_banner(body)

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

	var client := _idtx()
	if client == null:
		_show_error_status("IDTX client not available (GDExtension not loaded). Rebuild and restart the editor.")
		return

	# Fresh probe: clear any prior status and hide a stale login form while we
	# re-check reachability.
	_hide_status()
	_hide_login_panel()

	# Apply the base URL now and probe reachability before revealing the login
	# form, so a wrong/unreachable URL is caught up front.
	client.set_base_url(url)
	_connect_btn.disabled = true
	_connect_btn.text = "Checking…"
	client.health(_on_health_probe_done.bind(url))


func _on_health_probe_done(result: Dictionary, url: String) -> void:
	_connect_btn.disabled = false
	_connect_btn.text = "Connect"
	if bool(result.get("ok", false)):
		# Reachable → clear status and reveal the login form.
		_hide_status()
		_reveal_login_panel(url)
		return
	# Unreachable → show the error and make sure the login form is hidden.
	var msg := String(result.get("message", ""))
	if msg.is_empty():
		msg = "Server unreachable at %s (%d %s)." % [
			url, int(result.get("http_code", 0)), String(result.get("error_code", ""))]
	_hide_login_panel()
	_show_error_status(msg)
	push_warning("[IDTXFlow] [Select Source] %s" % msg)


func _reveal_login_panel(url: String) -> void:
	# Reveal the login panel (and its top separator) below the "or / local"
	# section, still on this same step.
	if _login_panel:
		var sep = _login_panel.get_meta("companion_separator", null)
		if sep is HSeparator:
			sep.visible = true
		_login_panel.show_for_url(url)


## Hide the login panel (and its companion separator) if currently shown.
func _hide_login_panel() -> void:
	if _login_panel:
		var sep = _login_panel.get_meta("companion_separator", null)
		if sep is HSeparator:
			sep.visible = false
		if _login_panel.has_method("hide_panel"):
			_login_panel.hide_panel()
		else:
			(_login_panel as Control).visible = false


func _idtx() -> Object:
	return IdtxAccess.get_client()


# ---------------------------------------------------------------------------
# Health-check status banner
# ---------------------------------------------------------------------------

func _build_status_banner(parent: VBoxContainer) -> void:
	_status_panel = PanelContainer.new()
	_status_panel.visible = false
	_status_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	var err_color := WizardTheme.get_error_color(self)
	var err_bg := err_color
	err_bg.a = 0.25
	var style := WizardTheme.make_solid_style(err_bg, 4)
	style.border_color = err_color
	style.border_width_left = 1
	style.border_width_top = 1
	style.border_width_right = 1
	style.border_width_bottom = 1
	style.content_margin_left = WizardTheme.px(10)
	style.content_margin_right = WizardTheme.px(10)
	style.content_margin_top = WizardTheme.px(8)
	style.content_margin_bottom = WizardTheme.px(8)
	_status_panel.add_theme_stylebox_override("panel", style)

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", WizardTheme.px(8))
	_status_panel.add_child(row)

	_status_icon = Label.new()
	_status_icon.text = "⊘"
	_status_icon.add_theme_color_override("font_color", err_color)
	row.add_child(_status_icon)

	_status_label = Label.new()
	_status_label.add_theme_color_override("font_color", err_color)
	_status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	row.add_child(_status_label)

	parent.add_child(_status_panel)


func _show_error_status(msg: String) -> void:
	if _status_label:
		_status_label.text = msg
	if _status_panel:
		_status_panel.visible = true


func _hide_status() -> void:
	if _status_panel:
		_status_panel.visible = false


func _on_login_succeeded(url: String, username: String, remember: bool) -> void:
	server_login_succeeded.emit(url, username, remember)


func _on_local_pressed() -> void:
	local_files_requested.emit()


func _on_cancel_pressed() -> void:
	cancel_requested.emit()
