@tool
extends VBoxContainer

## Import options step: a single 4-way "Import mode" choice + selected-asset preview.
##
## Presents the import options on a single screen. Layout:
##
##   Header: "Step 3 of 3 — Configure: Define import settings"
##   HSplit (same 2-panel layout/separation as the browse step)
##     LEFT  (options):  Label "Import Options:"  (OUTSIDE the card)
##                       Filled card (ItemListSecondary bg + scroll overlay):
##                         "Import mode" section — one radio group of four options
##     RIGHT (preview):  AssetDetailPanel (SELECTED_ASSET style)
##   Footer: [Back] … [Cancel] [Import]
##
## The four mutually-exclusive options are:
##
##   1. Import into current scene   — download only (no session), into the current scene
##   2. Import into new scene       — download only (no session), into a fresh scene
##   3. Create collaboration session — create + open a live session (mode: single_edit
##                                     / collaborative_edit); always a new scene
##   4. Join collaboration session   — join a running collaborative_edit session for the
##                                     selected USD path; always a new scene
##
## Options 3 and 4 are server-only (hidden for local imports via
## set_server_options_visible). Option 4 shows a read-only list of active
## collaborative sessions for the selected path (set_join_sessions); when the list
## is empty the option is disabled with an inline hint.
##
## The layout mirrors `asset_detail_panel.gd`: a HeaderSmall label sits *above*
## the filled content card (rather than inside it), and the sections read like
## the editor inspector — native `FoldableContainer` categories with two-column
## property rows, driven by the editor theme with minimal overrides.
##
## The footer primary button says "Import" and emits `confirm_requested`. Import is
## disabled whenever the selected action is not currently executable (Join selected
## with no valid session), surfaced via `can_import()` / `import_ready_changed`.

signal confirm_requested
signal back_requested
signal cancel_requested
## Emitted whenever the executability of the current selection changes, so the
## host/footer can enable or disable the Import button. `ready` == can_import().
signal import_ready_changed(ready: bool)
## Emitted when the user presses the Join "Refresh" button; the host re-queries
## the active sessions and calls set_join_sessions() with the fresh list.
signal refresh_sessions_requested

const WizardTheme  := preload("res://addons/IDTXFlow/editor/import/widgets/wizard_theme.gd")
const WizardHeader := preload("res://addons/IDTXFlow/editor/import/widgets/wizard_header.gd")
const WizardFooter := preload("res://addons/IDTXFlow/editor/import/widgets/wizard_footer.gd")
const AssetPanel   := preload("res://addons/IDTXFlow/editor/import/widgets/asset_detail_panel.gd")

# Import action identifiers (returned by get_import_action()).
const ACTION_CURRENT        := "current"
const ACTION_NEW            := "new"
const ACTION_CREATE_SESSION := "create_session"
const ACTION_JOIN_SESSION   := "join_session"

# Collaboration session mode identifiers (create-session only).
const MODE_SINGLE := "single_edit"
const MODE_COLLAB := "collaborative_edit"

var _asset_panel: Node
var _footer: Node

# Import-mode radio group (4 mutually-exclusive options).
var _radio_current: CheckBox
var _radio_new: CheckBox
var _radio_create: CheckBox
var _radio_join: CheckBox
var _target_info_label: Label

# Create-session controls (enabled only when "Create session" is selected).
var _create_mode_option: OptionButton

# Join-session controls (enabled only when "Join session" is selected).
var _join_list: ItemList
# Static description shown directly under the Join radio (always visible).
var _join_desc_label: Label
# Empty-state message shown only when no sessions are available.
var _join_empty_label: Label
# Re-query button for the session list.
var _join_refresh_btn: Button
# Session id per join_list row index, populated by set_join_sessions().
var _join_session_ids: PackedStringArray = PackedStringArray()
# Whether the "current scene" option is allowed (a scene is open).
var _current_enabled: bool = true
# Whether server-only options (Create / Join) are shown.
var _server_options_visible: bool = false

# Re-entrancy guard: `add_theme_stylebox_override` / `remove_theme_stylebox_override`
# emit `theme_changed`, which we listen to for re-applying the section tint.
# Without this guard that would recurse infinitely (stack overflow).
var _applying_header_style := false


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

	# Two-column split — mirrors the browse step's layout (same HSplit
	# separation, options as the left content, asset details on the right).
	var split := HSplitContainer.new()
	split.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_theme_constant_override("separation", WizardTheme.px(12))
	add_child(split)

	# LEFT: header ABOVE a filled card, mirroring the right Asset Details
	# panel. The header label is a direct child of the column (outside the
	# card), then the card is a Control hosting the ItemListSecondary theme
	# background with a scrollable content overlay on top.
	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(left)

	# Header — sits OUTSIDE / above the card (like "Asset Details:").
	var left_header := Label.new()
	left_header.text = "Import Options:"
	left_header.theme_type_variation = "HeaderSmall"
	left.add_child(left_header)

	# Filled card: ItemListSecondary background + scroll/margin content overlay.
	var card := Control.new()
	card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	card.size_flags_vertical = Control.SIZE_EXPAND_FILL
	card.custom_minimum_size = Vector2(0, WizardTheme.px(320))
	left.add_child(card)

	var card_bg := ItemList.new()
	card_bg.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	card_bg.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	card_bg.theme_type_variation = "ItemListSecondary"
	card_bg.focus_mode = Control.FOCUS_NONE
	card_bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	card.add_child(card_bg)

	var scroll := ScrollContainer.new()
	scroll.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.mouse_filter = Control.MOUSE_FILTER_PASS
	card.add_child(scroll)

	var margin := MarginContainer.new()
	margin.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var pad := WizardTheme.px(8)
	margin.add_theme_constant_override("margin_left", pad)
	margin.add_theme_constant_override("margin_right", pad)
	margin.add_theme_constant_override("margin_top", pad)
	margin.add_theme_constant_override("margin_bottom", pad)
	scroll.add_child(margin)

	# Content VBox: sections stacked like the inspector (tight rhythm).
	var content := VBoxContainer.new()
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	#content.add_theme_constant_override("separation", WizardTheme.px(8))
	content.add_theme_constant_override("separation", 0)
	margin.add_child(content)

	# Import mode — one radio group of four options in a single section.
	content.add_child(_build_import_mode_section())

	# RIGHT: selected-asset preview -------------------------------------
	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(WizardTheme.px(280), 0)
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_child(right)

	_asset_panel = AssetPanel.new()
	(_asset_panel as Control).size_flags_vertical = Control.SIZE_EXPAND_FILL
	(_asset_panel as Control).custom_minimum_size = Vector2(WizardTheme.px(260), WizardTheme.px(420))
	right.add_child(_asset_panel)
	if _asset_panel.has_method("set_header_style"):
		_asset_panel.set_header_style(1)  # SELECTED_ASSET

	# Footer — primary action is "Import"
	_footer = WizardFooter.new()
	add_child(_footer)
	_footer.setup(true, "Import", true)
	_footer.back_pressed.connect(func(): back_requested.emit())
	_footer.cancel_pressed.connect(func(): cancel_requested.emit())
	_footer.primary_pressed.connect(_on_import_pressed)

	# Reflect the initial selection's executability on the Import button.
	_notify_import_ready()


# ---------------------------------------------------------------------------
# Public API (called by import_manager.gd)
# ---------------------------------------------------------------------------

func set_selected_path(file_path: String) -> void:
	if _asset_panel and _asset_panel.has_method("populate"):
		_asset_panel.populate(file_path)


func set_selected_meta(meta: Dictionary) -> void:
	if _asset_panel and _asset_panel.has_method("populate_from_dict"):
		_asset_panel.populate_from_dict(meta)


## Updates the "Target:" info line beneath the "Import into current scene"
## radio. When `enabled` is false the current-scene option is greyed out and a
## still-enabled option is auto-selected.
func set_current_target_info(display_name: String, sub_text: String, enabled: bool) -> void:
	_current_enabled = enabled
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


## The selected import action: one of ACTION_CURRENT / ACTION_NEW /
## ACTION_CREATE_SESSION / ACTION_JOIN_SESSION.
func get_import_action() -> String:
	if _radio_new and _radio_new.button_pressed:
		return ACTION_NEW
	if _radio_create and _radio_create.button_pressed:
		return ACTION_CREATE_SESSION
	if _radio_join and _radio_join.button_pressed:
		return ACTION_JOIN_SESSION
	return ACTION_CURRENT


## The selected session mode string ("single_edit" / "collaborative_edit").
## Only meaningful when get_import_action() == ACTION_CREATE_SESSION.
func get_session_mode() -> String:
	if _create_mode_option and _create_mode_option.selected == 1:
		return MODE_COLLAB
	return MODE_SINGLE


## The session id of the currently selected join-list row, or "" if none.
## Only meaningful when get_import_action() == ACTION_JOIN_SESSION.
func get_selected_session_id() -> String:
	if _join_list == null:
		return ""
	var sel := _join_list.get_selected_items()
	if sel.is_empty():
		return ""
	var idx: int = sel[0]
	if idx < 0 or idx >= _join_session_ids.size():
		return ""
	return _join_session_ids[idx]


## Whether the currently selected action can be executed right now. Actions
## 1/2/3 are always executable; Join (action 4) requires a valid selected
## session. Drives the Import button's enabled state.
func can_import() -> bool:
	if get_import_action() == ACTION_JOIN_SESSION:
		return not get_selected_session_id().is_empty()
	return true


## Show the server-only options (Create / Join sessions) or hide them (local
## imports). When hidden, a hidden-but-selected option falls back to "new scene"
## so a local import can never be session-based.
func set_server_options_visible(show_it: bool) -> void:
	_server_options_visible = show_it
	if _radio_create:
		_radio_create.get_parent().visible = show_it
	if _radio_join:
		_radio_join.get_parent().visible = show_it
	if not show_it:
		if (_radio_create and _radio_create.button_pressed) \
				or (_radio_join and _radio_join.button_pressed):
			# A server-only option was selected; fall back to a valid one.
			if _current_enabled and _radio_current:
				_radio_current.button_pressed = true
			elif _radio_new:
				_radio_new.button_pressed = true
	_refresh_join_enabled()
	_notify_import_ready()


## Populate the join list with active collaborative sessions for the selected
## path. `sessions` is an Array of session dicts { session_id, usd_file, mode,
## client_count, ... }. An empty array disables the Join option with a hint. Also
## re-enables the Refresh button (a pending re-query has now completed).
func set_join_sessions(sessions: Array, joined_ids: PackedStringArray = PackedStringArray()) -> void:
	_join_session_ids = PackedStringArray()
	if _join_list:
		_join_list.clear()
		var first_selectable := -1
		for s in sessions:
			var sid := String(s.get("session_id", ""))
			if sid.is_empty():
				continue
			var count := int(s.get("client_count", 0))
			var already_joined := joined_ids.has(sid)
			var label := "%s  •  %d client(s)" % [sid, count]
			if already_joined:
				label += "  •  (joined)"
			var idx := _join_list.item_count
			_join_list.add_item(label)
			_join_session_ids.append(sid)
			if already_joined:
				# Can't join a session we are already in: disable the row so it
				# can't be selected (mirrors the wizard's client-side guard).
				_join_list.set_item_disabled(idx, true)
			elif first_selectable < 0:
				first_selectable = idx
		# Default-select the first row we can actually join.
		if first_selectable >= 0:
			_join_list.select(first_selectable)
	if _join_refresh_btn:
		_join_refresh_btn.disabled = false
	_refresh_join_enabled()
	_notify_import_ready()


# ---------------------------------------------------------------------------
# Import mode section — one radio group of four options
# ---------------------------------------------------------------------------

## Builds the single "Import mode" FoldableContainer holding all four mutually
## exclusive options. Options 3 (Create) and 4 (Join) are wrapped in containers
## kept hidden for local imports (toggled by set_server_options_visible).
func _build_import_mode_section() -> Control:
	var fc := _make_section("Import mode")
	var body := _get_section_content(fc)

	# CheckBox controls in a shared ButtonGroup behave as mutually exclusive
	# radio buttons.
	var group := ButtonGroup.new()

	# --- Option 1: current scene (download only) -----------------------
	_radio_current = CheckBox.new()
	_radio_current.text = "Import into current scene"
	_radio_current.button_group = group
	_radio_current.button_pressed = true
	_radio_current.toggled.connect(_on_action_toggled)
	body.add_child(_radio_current)

	# Muted description under the current-scene radio (native Label styling).
	_target_info_label = _make_inline_caption("Target: -")
	body.add_child(_make_caption_indent(_target_info_label))

	# --- Option 2: new scene (download only) ---------------------------
	_radio_new = CheckBox.new()
	_radio_new.text = "Import into new scene"
	_radio_new.button_group = group
	_radio_new.toggled.connect(_on_action_toggled)
	body.add_child(_radio_new)

	body.add_child(_make_caption_indent(_make_inline_caption(
		"A new scene will be created with the imported USD stage as its root."
	)))

	# --- Option 3: create collaboration session (server only) ----------
	# Wrapped in a VBox so set_server_options_visible() can hide the whole
	# option (radio + its mode dropdown + hint) in one shot for local imports.
	var create_box := VBoxContainer.new()
	create_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	create_box.add_theme_constant_override("separation", 0)
	body.add_child(create_box)

	_radio_create = CheckBox.new()
	_radio_create.text = "Create collaboration session"
	_radio_create.button_group = group
	_radio_create.toggled.connect(_on_action_toggled)
	create_box.add_child(_radio_create)

	# Session mode: enabled only when "Create session" is selected.
	_create_mode_option = OptionButton.new()
	_create_mode_option.add_item("Single edit (only you)")               # index 0 -> single_edit
	_create_mode_option.add_item("Collaborative edit (others can join)")  # index 1 -> collaborative_edit
	_create_mode_option.selected = 0
	_create_mode_option.disabled = true
	create_box.add_child(_make_caption_indent(_create_mode_option))

	create_box.add_child(_make_caption_indent(_make_inline_caption(
		"Creates a live editing session (WebSocket) and imports into a new scene."
		+ " Single-edit locks the file to you; collaborative lets other clients join."
	)))

	# --- Option 4: join collaboration session (server only) ------------
	var join_box := VBoxContainer.new()
	join_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	join_box.add_theme_constant_override("separation", 0)
	body.add_child(join_box)

	_radio_join = CheckBox.new()
	_radio_join.text = "Join collaboration session"
	_radio_join.button_group = group
	_radio_join.toggled.connect(_on_action_toggled)
	join_box.add_child(_radio_join)

	# Static description - directly under the radio (stays on top whether
	# or not the list is shown).
	_join_desc_label = _make_inline_caption(
		"Joins a running collaborative session and imports into a new scene."
	)
	join_box.add_child(_make_caption_indent(_join_desc_label))

	# "Active sessions" header row + Refresh button, so the list can be re-queried
	# without leaving/re-entering the step.
	var refresh_row := HBoxContainer.new()
	refresh_row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	refresh_row.add_theme_constant_override("separation", WizardTheme.px(6))
	var sessions_label := _make_inline_caption("Active sessions")
	sessions_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	refresh_row.add_child(sessions_label)
	_join_refresh_btn = Button.new()
	_join_refresh_btn.text = "Refresh"
	_join_refresh_btn.tooltip_text = "Re-query active collaborative sessions for this file."
	var reload_icon := WizardTheme.get_editor_icon(self, "Reload", "Reload")
	if reload_icon:
		_join_refresh_btn.icon = reload_icon
	_join_refresh_btn.pressed.connect(_on_join_refresh_pressed)
	refresh_row.add_child(_join_refresh_btn)
	join_box.add_child(_make_caption_indent(refresh_row))

	# Session list: the active collaborative sessions for the selected path.
	_join_list = ItemList.new()
	_join_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_join_list.custom_minimum_size = Vector2(0, WizardTheme.px(96))
	_join_list.select_mode = ItemList.SELECT_SINGLE
	_join_list.item_selected.connect(_on_join_item_selected)
	join_box.add_child(_make_caption_indent(_join_list))

	# Empty-state message — shown ONLY when the list is empty (below the list slot).
	_join_empty_label = _make_inline_caption(
		"No active collaborative sessions for this file. Try Refresh."
	)
	join_box.add_child(_make_caption_indent(_join_empty_label))

	# Server-only options start hidden; the manager reveals them for server
	# imports via set_server_options_visible().
	create_box.visible = false
	join_box.visible = false

	return fc


# ---------------------------------------------------------------------------
# Description-text helper (plain, native Label — minimal overrides)
# ---------------------------------------------------------------------------

## A caption Label that autowraps. Uses the default (theme-driven) Label text
## color — no muted override — so it matches the inspector's body text.
func _make_inline_caption(text: String) -> Label:
	var lbl := Label.new()
	lbl.text = text
	lbl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return lbl


## Wrap a caption in a consistent left indent so it aligns under the radio's
## label rather than its checkbox — indented "one tab more in" (past the
## checkbox glyph plus a tab).
func _make_caption_indent(child: Control) -> HBoxContainer:
	var indent := HBoxContainer.new()
	indent.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	indent.add_theme_constant_override("separation", 0)
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(WizardTheme.px(34), 0)
	indent.add_child(spacer)
	child.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	indent.add_child(child)
	return indent


# ---------------------------------------------------------------------------
# Selection handlers + Import-readiness
# ---------------------------------------------------------------------------

## Any of the four radios toggled. Update the per-option enabled sub-controls
## (create mode dropdown, join list) and the Import button's readiness.
func _on_action_toggled(_pressed: bool) -> void:
	var action := get_import_action()
	if _create_mode_option:
		_create_mode_option.disabled = action != ACTION_CREATE_SESSION
	_refresh_join_enabled()
	_notify_import_ready()


func _on_join_item_selected(_index: int) -> void:
	# A join row was picked; readiness may have changed (Join now executable).
	_notify_import_ready()


## Pressed the Join "Refresh" button. Disable it to avoid a double-fire while the
## request is in flight (re-enabled by set_join_sessions when results arrive) and
## ask the host to re-query.
func _on_join_refresh_pressed() -> void:
	if _join_refresh_btn:
		_join_refresh_btn.disabled = true
	refresh_sessions_requested.emit()


## Enable/disable the Join option based on whether any sessions are available.
## The static description stays visible on top at all times; the list is shown
## only when populated, and the empty-state message only when empty. When empty
## and Join is selected, selection falls back to a still-valid option.
func _refresh_join_enabled() -> void:
	if _radio_join == null or _join_list == null:
		return

	var has_sessions := _join_list.item_count > 0
	_radio_join.disabled = not has_sessions
	_join_list.visible = has_sessions

	if _join_desc_label:
		_join_desc_label.visible = true
	if _join_empty_label:
		_join_empty_label.visible = not has_sessions

	# Don't leave a disabled radio selected.
	if not has_sessions and _radio_join.button_pressed:
		if _current_enabled and _radio_current:
			_radio_current.button_pressed = true
		elif _radio_new:
			_radio_new.button_pressed = true


## Recompute can_import() and reflect it on the footer's Import button, emitting
## import_ready_changed so external listeners can mirror the state.
func _notify_import_ready() -> void:
	var ready := can_import()
	if _footer and _footer.has_method("set_primary_enabled"):
		_footer.set_primary_enabled(ready)
	import_ready_changed.emit(ready)


## Import pressed: guard against a stale/invalid selection (belt-and-suspenders
## on top of the disabled button) before forwarding the confirm.
func _on_import_pressed() -> void:
	if not can_import():
		return
	confirm_requested.emit()


# ---------------------------------------------------------------------------
# Section helper
# ---------------------------------------------------------------------------

## A foldable settings group that reads like the inspector's collapsible
## sub-sections (Transform / Visibility / …). It uses the native
## `FoldableContainer` theme; `_apply_section_header_style` then adds the
## inspector's faint, transparency-based header tint (same color at low alpha,
## not a different hue) so it reads like the editor without heavy overrides.
func _make_section(title: String) -> FoldableContainer:
	var fc := FoldableContainer.new()
	fc.title = title
	fc.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	fc.folded = false
	# Re-apply the tint on every theme change (not just once): when the editor
	# theme switches, the native FoldableContainer styleboxes change (corners,
	# margins, base color). A one-shot apply would leave stale overrides — most
	# visibly the collapsed header diverging from the inspector. The inspector
	# itself recomputes its section bg on NOTIFICATION_THEME_CHANGED. We also
	# apply once now for the initial (current) theme.
	fc.theme_changed.connect(_apply_section_header_style.bind(fc))
	_apply_section_header_style(fc)

	var content := VBoxContainer.new()
	content.name = "Content"
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	# Inspector-tight row rhythm: rows stack with no gap between them.
	#content.add_theme_constant_override("separation", 0)
	fc.add_child(content)

	return fc


## Reproduce the inspector's section-header tint the way the inspector does it:
## the theme's `prop_subsection` color drawn at LOW ALPHA (≈ a *= 0.4) over the
## card — same color, transparency-based, not a different hue and not an opaque
## fill. Native content margins and corner radius are preserved from the
## existing FoldableContainer theme so padding/rounding stay theme-driven; only
## the (semi-transparent) background color is overridden. Hover uses the
## inspector's lighten(0.2) on the same low-alpha color.
func _apply_section_header_style(fc: FoldableContainer) -> void:
	# Guard against infinite recursion: the add/remove theme override calls
	# below emit `theme_changed`, which is connected back to this function.
	# Re-entrant calls simply return until the outer call finishes.
	if _applying_header_style:
		return
	_applying_header_style = true

	# Clear our own previous overrides FIRST so that _section_header_sb clones
	# the *native* stylebox for the current theme (otherwise get_theme_stylebox
	# would return our stale tinted override and corners/margins/tint compound
	# across theme changes).
	const TITLE_STYLES := [
		"title_panel",
		"title_hover_panel",
		"title_collapsed_panel",
		"title_collapsed_hover_panel",
	]
	for style_name in TITLE_STYLES:
		if fc.has_theme_stylebox_override(style_name):
			fc.remove_theme_stylebox_override(style_name)

	# Header tint: reuse the inspector's `prop_subsection` color at low alpha so the
	# section header reads as a faint transparency-based band (idle) with a slight
	# lift on hover — theme-driven, no hardcoded hues.
	var tint := WizardTheme.get_subsection_color(fc)
	tint.a *= 0.4

	var hover_tint := tint.lightened(0.2)
	
	# Apply the tint to all four title states (idle/hover × expanded/collapsed) by
	# cloning the native stylebox and only swapping bg_color — margins/corners
	fc.add_theme_stylebox_override("title_panel", _section_header_sb(fc, "title_panel", tint))
	fc.add_theme_stylebox_override("title_hover_panel", _section_header_sb(fc, "title_hover_panel", hover_tint))
	fc.add_theme_stylebox_override("title_collapsed_panel", _section_header_sb(fc, "title_collapsed_panel", tint))
	fc.add_theme_stylebox_override("title_collapsed_hover_panel", _section_header_sb(fc, "title_collapsed_hover_panel", hover_tint))
	
	# Force the collapsed-idle title text to the normal `font_color` so it stops
	# rendering with the editor accent (blue) and matches the other three states.
	fc.add_theme_color_override("collapsed_font_color", fc.get_theme_color("font_color"))
	
	# Hide the accent-colored keyboard-focus outline drawn around the header;
	# focus behavior itself is unaffected, only the visual rectangle is removed.
	fc.add_theme_stylebox_override("focus", StyleBoxEmpty.new())

	# Align the body content with the header on all sides, and start the rows'
	# left edge under the TITLE TEXT (so "A/B/C/…" begin under the "P" of
	# "Prim Types"). The native `panel` (body) stylebox has its own margins that
	# don't match the title's text offset, causing the mismatch/padding you see.
	# We override the body `panel` margins:
	#   left  = title text x-offset  (title stylebox left margin + fold arrow
	#           icon width + the header's h_separation)
	#   right = title stylebox right margin
	#   top/bottom = a small consistent inset
	if fc.has_theme_stylebox_override("panel"):
		fc.remove_theme_stylebox_override("panel")

	var title_sb_ref := fc.get_theme_stylebox("title_panel")
	var title_left := title_sb_ref.content_margin_left if title_sb_ref else float(WizardTheme.px(4))
	var title_right := title_sb_ref.content_margin_right if title_sb_ref else float(WizardTheme.px(4))

	var arrow := fc.get_theme_icon("expanded_arrow") if fc.has_theme_icon("expanded_arrow") else null
	var arrow_w := float(arrow.get_width()) if arrow else float(WizardTheme.px(16))
	var h_sep := float(fc.get_theme_constant("h_separation")) if fc.has_theme_constant("h_separation") else float(WizardTheme.px(4))

	var body_sb := StyleBoxEmpty.new()
	body_sb.content_margin_left = title_left + arrow_w + h_sep
	#body_sb.content_margin_right = title_right
	body_sb.content_margin_right = 0
	#body_sb.content_margin_top = float(WizardTheme.px(4))
	body_sb.content_margin_top = 0
	#body_sb.content_margin_bottom = float(WizardTheme.px(4))
	body_sb.content_margin_bottom = 0
	fc.add_theme_stylebox_override("panel", body_sb)

	_applying_header_style = false


## Build a header stylebox by cloning the native FoldableContainer stylebox
## (to keep its margins/corners/theme-driven look) and only swapping in the
## semi-transparent background color. Falls back to a bare StyleBoxFlat if the
## native one isn't a StyleBoxFlat.
func _section_header_sb(fc: FoldableContainer, native_name: String, bg: Color) -> StyleBox:
	var native := fc.get_theme_stylebox(native_name)
	if native is StyleBoxFlat:
		var sb := (native as StyleBoxFlat).duplicate() as StyleBoxFlat
		sb.bg_color = bg
		return sb
	var flat := StyleBoxFlat.new()
	flat.bg_color = bg
	return flat


func _get_section_content(section: FoldableContainer) -> VBoxContainer:
	return section.get_node("Content") as VBoxContainer


# ---------------------------------------------------------------------------
# Property row helpers (inspector two-column layout)
#
# Generic, reusable building blocks for inspector-style option rows. Kept
# available for future import options; not tied to any specific setting.
# ---------------------------------------------------------------------------

## Inspector-style property row: label in the LEFT column (plain, transparent)
## and the editor in the RIGHT column wrapped in a subtly DARKER cell — mirroring
## the inspector, where each row's value side reads as a slightly darker box
## (the field's own filled background) against the panel behind it.
##
## The darkening is a relative semi-transparent black overlay (not a fixed hue),
## so it stays "slightly darker" on any editor theme, and it applies uniformly
## to every row type — including checkboxes, which otherwise have no field box.
func _make_property_row(caption: String, editor: Control) -> Control:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_theme_constant_override("separation", WizardTheme.px(8))

	var lbl := Label.new()
	lbl.text = caption
	lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lbl.size_flags_stretch_ratio = 1.0
	row.add_child(lbl)

	# Right (value) cell: a subtly darker PanelContainer so the value side is
	# visually separated from the label, like the inspector's field boxes.
	var value_cell := PanelContainer.new()
	value_cell.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	value_cell.size_flags_stretch_ratio = 1.0
	var value_bg := StyleBoxFlat.new()
	value_bg.bg_color = Color(0, 0, 0, 0.12)  # relative darkening, theme-agnostic
	var vpad := WizardTheme.px(4)
	value_bg.content_margin_left = vpad
	value_bg.content_margin_right = vpad
	value_bg.content_margin_top = WizardTheme.px(2)
	value_bg.content_margin_bottom = WizardTheme.px(2)
	var vradius := WizardTheme.corner_radius(3)
	value_bg.corner_radius_top_left = vradius
	value_bg.corner_radius_top_right = vradius
	value_bg.corner_radius_bottom_left = vradius
	value_bg.corner_radius_bottom_right = vradius
	value_cell.add_theme_stylebox_override("panel", value_bg)
	row.add_child(value_cell)

	editor.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	value_cell.add_child(editor)

	return row


func _make_checkbox_row(caption: String, checked: bool) -> Control:
	var cb := CheckBox.new()
	cb.button_pressed = checked
	return _make_property_row(caption, cb)


## Numeric setting row using the inspector's own `EditorSpinSlider`, letting it
## choose its *preferred* presentation from the value type (exactly like the
## Inspector dock):
##
##   • integer values  → up/down arrows (SpinBox-like)
##   • float values    → slider
##
## This is driven by `control_state = CONTROL_STATE_DEFAULT` plus the
## `editing_integer` flag. The caption stays in the LEFT column (no internal
## label) and the field fills the RIGHT column, drawing its own value box
## (theme-driven contrast, same as the inspector).
##
## `EditorSpinSlider` is editor-only; fall back to a plain `SpinBox` (which also
## shows arrows and supports floats) if it isn't available.
func _make_spin_row(caption: String, value: float, min_v: float, max_v: float, step: float) -> Control:
	# Integer when the step and bounds/value are all whole numbers.
	var is_int := (
		is_equal_approx(step, roundf(step))
		and is_equal_approx(value, roundf(value))
		and is_equal_approx(min_v, roundf(min_v))
		and is_equal_approx(max_v, roundf(max_v))
	)

	if ClassDB.class_exists("EditorSpinSlider"):
		const CONTROL_STATE_DEFAULT := 0
		var ess := ClassDB.instantiate("EditorSpinSlider")
		# No internal label — the caption is the left column.
		# `flat = true` so the spinner does NOT draw its own background box; the
		# shared darker value cell from `_make_property_row` is the only box, so
		# numeric rows match the checkbox rows exactly. (flat only removes the
		# background — the arrows/slider control still render.)
		ess.set("flat", true)
		ess.set("control_state", CONTROL_STATE_DEFAULT)  # preferred design per type
		ess.set("editing_integer", is_int)               # int → arrows, float → slider
		ess.set("min_value", min_v)
		ess.set("max_value", max_v)
		ess.set("step", step)
		ess.set("value", value)
		ess.set("allow_greater", true)
		ess.set("allow_lesser", false)
		(ess as Control).size_flags_horizontal = Control.SIZE_EXPAND_FILL
		(ess as Control).custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
		return _make_property_row(caption, ess as Control)

	# Fallback: plain SpinBox (arrows + float support).
	var sb := SpinBox.new()
	sb.min_value = min_v
	sb.max_value = max_v
	sb.step = step
	sb.value = value
	sb.custom_minimum_size = Vector2(0, WizardTheme.px(WizardTheme.INPUT_HEIGHT))
	return _make_property_row(caption, sb)

