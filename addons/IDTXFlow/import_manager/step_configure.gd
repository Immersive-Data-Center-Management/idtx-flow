@tool
extends VBoxContainer

## Import options step: destination + settings in one place.
##
## Presents all import options (destination, prim types, definition, settings) on a
## single screen. Layout:
##
##   Header: "Step 3 of 3 — Configure: Define import settings"
##   HSplit (same 2-panel layout/separation as the browse step)
##     LEFT  (options):  Label "Import Options:"  (OUTSIDE the card)
##                       Filled card (ItemListSecondary bg + scroll overlay):
##                         Import destination → Prim Types →
##                         Import Definition → Import Settings
##     RIGHT (preview):  AssetDetailPanel (SELECTED_ASSET style)
##   Footer: [Back] … [Cancel] [Import]
##
## The layout mirrors `asset_detail_panel.gd`: a HeaderSmall label sits *above*
## the filled content card (rather than inside it), and the sections read like
## the editor inspector — native `FoldableContainer` categories with two-column
## property rows, driven by the editor theme with minimal overrides.
##
## The footer primary button says "Import" and emits `confirm_requested`.

signal confirm_requested
signal back_requested
signal cancel_requested
signal destination_changed(destination: String)

const WizardTheme  := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")
const WizardHeader := preload("res://addons/IDTXFlow/import_manager/wizard_header.gd")
const WizardFooter := preload("res://addons/IDTXFlow/import_manager/wizard_footer.gd")
const AssetPanel   := preload("res://addons/IDTXFlow/import_manager/asset_detail_panel.gd")

# Destination string identifiers.
const DEST_CURRENT := "current"
const DEST_NEW     := "new"

# Collaboration session mode identifiers (server imports only).
const MODE_SINGLE := "single_edit"
const MODE_COLLAB := "collaborative_edit"

var _asset_panel: Node

var _radio_current: CheckBox
var _radio_new: CheckBox
var _target_info_label: Label

# Collaboration (server-only) opt-in: when off, a server import just downloads
# the file (like a local import); when on, it opens a live session over WebSocket.
var _collab_section: Control
var _collab_checkbox: CheckBox
var _collab_mode_option: OptionButton

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

	# Import destination — same collapsible-section rhythm as the others.
	content.add_child(_build_destination_section())

	# Collaboration section (server imports only; hidden for local). Shown/hidden
	# by the manager via set_collaboration_option_visible().
	_collab_section = _build_collaboration_section()
	content.add_child(_collab_section)

	# Settings sections in two sub-columns beneath the destination.
	var cols := HBoxContainer.new()
	cols.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cols.add_theme_constant_override("separation", 0)
	#cols.add_theme_constant_override("separation", WizardTheme.px(12))
	#cols.add_theme_constant_override("separation", 0)
	content.add_child(cols)

	var left_col := VBoxContainer.new()
	left_col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	#left_col.add_theme_constant_override("separation", WizardTheme.px(6))
	left_col.add_theme_constant_override("separation", 0)
	cols.add_child(left_col)

	var prim_types := _make_section("Prim Types")
	left_col.add_child(prim_types)
	var prim_body := _get_section_content(prim_types)
	prim_body.add_child(_make_checkbox_row("A", true))
	prim_body.add_child(_make_checkbox_row("B", true))
	prim_body.add_child(_make_checkbox_row("C", true))
	prim_body.add_child(_make_checkbox_row("D", false))

	var right_col := VBoxContainer.new()
	right_col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	#right_col.add_theme_constant_override("separation", WizardTheme.px(6))
	right_col.add_theme_constant_override("separation", 0)
	cols.add_child(right_col)

	var import_def := _make_section("Import Definition")
	right_col.add_child(import_def)
	var def_body := _get_section_content(import_def)
	def_body.add_child(_make_checkbox_row("Cameras", true))
	def_body.add_child(_make_checkbox_row("Lights", true))

	var import_settings := _make_section("Import Settings")
	right_col.add_child(import_settings)
	var settings_body := _get_section_content(import_settings)
	settings_body.add_child(_make_spin_row("Scale", 1.0, 0.001, 1000.0, 0.001))
	settings_body.add_child(_make_spin_row("Light Intensity", 1.0, 0.0, 100.0, 0.1))

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

	# Footer — primary is "Import" now (was "Next").
	var footer := WizardFooter.new()
	add_child(footer)
	footer.setup(true, "Import", true)
	footer.back_pressed.connect(func(): back_requested.emit())
	footer.cancel_pressed.connect(func(): cancel_requested.emit())
	footer.primary_pressed.connect(func(): confirm_requested.emit())


# ---------------------------------------------------------------------------
# Public API (kept for import_manager.gd compatibility)
# ---------------------------------------------------------------------------

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


## True when the operator opted into a live collaboration session (server only).
func get_session_based() -> bool:
	return _collab_checkbox != null and _collab_checkbox.button_pressed


## The selected session mode string ("single_edit" / "collaborative_edit").
## Only meaningful when get_session_based() is true.
func get_session_mode() -> String:
	if _collab_mode_option and _collab_mode_option.selected == 1:
		return MODE_COLLAB
	return MODE_SINGLE


## Show the collaboration section (server imports) or hide it (local imports).
## When hidden, the checkbox is forced off so a local import can never be
## session-based.
func set_collaboration_option_visible(show_it: bool) -> void:
	if _collab_section:
		_collab_section.visible = show_it
	if not show_it and _collab_checkbox:
		_collab_checkbox.button_pressed = false
		if _collab_mode_option:
			_collab_mode_option.disabled = true


# ---------------------------------------------------------------------------
# Import destination section (native FoldableContainer, matching the others)
# ---------------------------------------------------------------------------

func _build_destination_section() -> Control:
	var fc := _make_section("Import destination")
	var body := _get_section_content(fc)

	# CheckBox controls in a shared ButtonGroup behave as mutually exclusive
	# radio buttons.
	var group := ButtonGroup.new()

	# --- Option 1: current scene ---------------------------------------
	_radio_current = CheckBox.new()
	_radio_current.text = "Import into current scene"
	_radio_current.button_group = group
	_radio_current.button_pressed = true
	_radio_current.toggled.connect(_on_radio_current_toggled)
	body.add_child(_radio_current)

	# Muted description under the current-scene radio (native Label styling).
	_target_info_label = _make_inline_caption("Target: -")
	body.add_child(_make_caption_indent(_target_info_label))

	# --- Option 2: new scene -------------------------------------------
	_radio_new = CheckBox.new()
	_radio_new.text = "Import into new scene"
	_radio_new.button_group = group
	_radio_new.toggled.connect(_on_radio_new_toggled)
	body.add_child(_radio_new)

	# Muted description under the new-scene radio (same plain style).
	body.add_child(_make_caption_indent(_make_inline_caption(
		"A new scene will be created with the imported USD stage as its root."
	)))

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
# Radio handlers
# ---------------------------------------------------------------------------

func _on_radio_current_toggled(pressed: bool) -> void:
	if pressed:
		destination_changed.emit(DEST_CURRENT)


func _on_radio_new_toggled(pressed: bool) -> void:
	if pressed:
		destination_changed.emit(DEST_NEW)


# ---------------------------------------------------------------------------
# Collaboration section (server imports only)
# ---------------------------------------------------------------------------

## A server import defaults to a plain authenticated download (like a local
## import). Turning on "Import as collaboration session" instead opens a live
## editing session over WebSocket; the mode dropdown then selects single- vs
## collaborative-edit. The dropdown is enabled only while the checkbox is on.
func _build_collaboration_section() -> Control:
	var fc := _make_section("Collaboration")
	var body := _get_section_content(fc)

	_collab_checkbox = CheckBox.new()
	_collab_checkbox.text = "Import as collaboration session"
	_collab_checkbox.button_pressed = false
	_collab_checkbox.toggled.connect(_on_collab_toggled)
	body.add_child(_collab_checkbox)

	# Session mode: enabled only when the checkbox is on.
	_collab_mode_option = OptionButton.new()
	_collab_mode_option.add_item("Single edit (only you)")       # index 0 -> single_edit
	_collab_mode_option.add_item("Collaborative edit (others can join)")  # index 1 -> collaborative_edit
	_collab_mode_option.selected = 0
	_collab_mode_option.disabled = true
	body.add_child(_make_caption_indent(_collab_mode_option))

	var hint := _make_inline_caption(
		"Opens a live editing session (WebSocket). Single-edit locks the file to you;"
		+ " collaborative lets other clients join the same session. Leave off to just"
		+ " download the file."
	)
	body.add_child(_make_caption_indent(hint))

	return fc


func _on_collab_toggled(pressed: bool) -> void:
	if _collab_mode_option:
		_collab_mode_option.disabled = not pressed


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
