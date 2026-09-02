@tool
extends VBoxContainer

## Step 2 - Browse the asset SERVER for USD files.
##
## Thin wrapper (mirrors `step_browse_files.gd`): builds a `WizardHeader` + a
## shared `WizardFileBrowser` (driven by a `ServerFileProvider`) + a
## `WizardFooter`. The provider talks to the real IDTX backend via the
## `IdtxClient` engine singleton (GET /api/v1/files) and presents the flat file
## list grouped by directory. Selecting a file emits `file_selected(path, meta)`
## and enables Next; the selection contract (get_selected_path/meta) is unchanged
## so import_manager.gd keeps working.
##
## The browser's right pane shows the `AssetDetailPanel` widget plugged in via
## `WizardFileBrowser.set_side_panel(...)`, populated from the backend metadata.

signal file_selected(path: String, meta: Dictionary)
signal back_requested
signal cancel_requested
signal next_requested

const WizardTheme         := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader        := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter        := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const WizardFileBrowser   := preload("res://addons/IDTXFlow/import_manager/wizard_file_browser.gd")
const AssetPanel          := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")
const ServerFileProvider  := preload("res://addons/IDTXFlow/import_manager/server_file_provider.gd")

var _server_url: String = ""
var _selected_path: String = ""
var _selected_meta: Dictionary = {}

var _browser: Node
var _provider: RefCounted
var _footer: Node
var _detail_panel: Node
var _status_label: Label


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(2, 3, "Browse:", "Select a file to import from the asset server")

	_provider = ServerFileProvider.new()
	_provider.set_server_url(_server_url)

	_browser = WizardFileBrowser.new()
	_browser.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_browser.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(_browser)

	# Restrict to USD extensions, matching the local browse step.
	_browser.set_file_provider(_provider)
	_browser.set_file_mode(WizardFileBrowser.FileMode.FILE_MODE_OPEN_FILE)
	_browser.set_filters(PackedStringArray([
		"*.usd,*.usda,*.usdc,*.usdz;USD Files;model/vnd.usd",
	]))
	_browser.set_all_files_option_enabled(false)

	# Plug the self-contained AssetDetailPanel into the browser's right pane.
	_detail_panel = AssetPanel.new()
	_browser.set_side_panel(_detail_panel)

	_browser.file_selected.connect(_on_browser_file_selected)
	_browser.listing_status.connect(_on_listing_status)

	# Async listing status line (Loading… / N file(s) / errors).
	_status_label = Label.new()
	_status_label.modulate = Color(1, 1, 1, 0.6)
	add_child(_status_label)

	_footer = WizardFooter.new()
	add_child(_footer)
	_footer.setup(true, "Next", false)
	_footer.back_pressed.connect(func(): back_requested.emit())
	_footer.cancel_pressed.connect(func(): cancel_requested.emit())
	_footer.primary_pressed.connect(_on_next_pressed)
	_footer.set_primary_enabled(false)


# ---------------------------------------------------------------------------
# Browser events
# ---------------------------------------------------------------------------

func _on_browser_file_selected(path: String, meta: Dictionary) -> void:
	_selected_path = path
	_selected_meta = meta
	if _detail_panel and _detail_panel.has_method("set_header_style"):
		_detail_panel.set_header_style(0)  # ASSET_DETAILS
	if _detail_panel and _detail_panel.has_method("populate_from_dict"):
		_detail_panel.populate_from_dict(meta)
	if _footer:
		_footer.set_primary_enabled(true)
	file_selected.emit(path, meta)


func _on_listing_status(message: String) -> void:
	if _status_label:
		_status_label.text = message


func _on_next_pressed() -> void:
	if _selected_path.is_empty():
		return
	next_requested.emit()


# ---------------------------------------------------------------------------
# Public API (kept for import_manager.gd compatibility)
# ---------------------------------------------------------------------------

func set_server_url(url: String) -> void:
	_server_url = url
	if _provider and _provider.has_method("set_server_url"):
		_provider.set_server_url(url)


func reset() -> void:
	_selected_path = ""
	_selected_meta = {}
	if _detail_panel and _detail_panel.has_method("populate_from_dict"):
		_detail_panel.populate_from_dict({})
	if _footer:
		_footer.set_primary_enabled(false)
	if _browser:
		# Re-list from the server root.
		_browser.set_current_dir(_server_url)


func get_selected_path() -> String:
	return _selected_path


func get_selected_meta() -> Dictionary:
	return _selected_meta