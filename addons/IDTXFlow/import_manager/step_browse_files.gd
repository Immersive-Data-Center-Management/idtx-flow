@tool
extends VBoxContainer

## Step 2 - Browse res:// for USD files.
##
## Thin wrapper: builds a `WizardHeader` + a `WizardFileBrowser` (which mirrors
## Godot's `FileDialog` interior) + a `WizardFooter`. The heavy lifting lives
## in `wizard_file_browser.gd`.
##
## The wizard's right pane shows the `AssetDetailPanel` widget plugged in via
## `WizardFileBrowser.set_side_panel(...)`.

signal file_selected(path: String)
signal back_requested
signal cancel_requested
signal next_requested

const WizardTheme       := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader      := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter      := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const WizardFileBrowser := preload("res://addons/IDTXFlow/import_manager/wizard_file_browser.gd")
const AssetPanel        := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")

const ROOT_PATH := "res://"

var _browser: Node
var _footer: Node
var _detail_panel: Node

var _selected_file: String = ""


func _init() -> void:
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_theme_constant_override("separation", WizardTheme.px(10))


func _ready() -> void:
	_build()


func _build() -> void:
	var header := WizardHeader.new()
	add_child(header)
	header.setup(2, 4, "Browse:", "Select file to import")

	_browser = WizardFileBrowser.new()
	_browser.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_browser.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(_browser)

	# Restrict to res:// and USD extensions.
	_browser.set_access(WizardFileBrowser.Access.ACCESS_RESOURCES)
	_browser.set_file_mode(WizardFileBrowser.FileMode.FILE_MODE_OPEN_FILE)
	_browser.set_filters(PackedStringArray([
		"*.usd,*.usda,*.usdc,*.usdz;USD Files;model/vnd.usd",
	]))
	# We only want USD files here - drop the "All Files (*.*)" fallback so
	# the user can't accidentally pick something we can't import.
	_browser.set_all_files_option_enabled(false)
	_browser.set_current_dir(ROOT_PATH)

	# Plug the self-contained AssetDetailPanel into the browser's right
	# pane. The panel supplies its own "Asset Details" header + content
	# card, so the wizard's built-in placeholder is replaced entirely.
	_detail_panel = AssetPanel.new()
	_browser.set_side_panel(_detail_panel)

	_browser.file_selected.connect(_on_browser_file_selected)

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

func _on_browser_file_selected(path: String) -> void:
	_selected_file = path
	if _detail_panel and _detail_panel.has_method("populate"):
		_detail_panel.populate(path)
	if _footer:
		_footer.set_primary_enabled(true)
	file_selected.emit(path)


func _on_next_pressed() -> void:
	if _selected_file.is_empty():
		return
	next_requested.emit()


# ---------------------------------------------------------------------------
# Public API (kept for import_manager.gd compatibility)
# ---------------------------------------------------------------------------

func reset() -> void:
	_selected_file = ""
	if _browser:
		_browser.set_current_dir(ROOT_PATH)
	# Reset the detail card back to its empty state on step re-entry (the
	# panel is always visible on step 2 — we don't hide it).
	if _detail_panel and _detail_panel.has_method("populate"):
		_detail_panel.populate("")
	if _footer:
		_footer.set_primary_enabled(false)


func get_selected_path() -> String:
	return _selected_file