@tool
extends VBoxContainer

## Reusable wizard header: progress line + step label.
##
## Layout:
##   Row 1 : "Step N of M - " + <bold_label> + <normal_label>
##   Row 2 : thin (3 px) ProgressBar (no percentage text)
##   Row 3 : subtle HSeparator

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")

var _step_label: RichTextLabel
var _progress: ProgressBar


func _init() -> void:
	add_theme_constant_override("separation", WizardTheme.px(6))


func setup(current_step: int, total_steps: int, bold_label: String, normal_label: String) -> void:
	_ensure_built()
	var text := "[font_size=%d][color=#8a8f99]Step %d of %d — [/color][b][color=#d9dce0]%s[/color][/b][color=#d9dce0] %s[/color][/font_size]" % [
		WizardTheme.fs(WizardTheme.FONT_SIZE_BODY), current_step, total_steps, bold_label, normal_label
	]
	_step_label.clear()
	_step_label.append_text(text)
	var pct := float(current_step) / float(max(total_steps, 1)) * 100.0
	_progress.value = pct


func _ensure_built() -> void:
	if _step_label != null:
		return

	# Row 1 --------------------------------------------------------------
	_step_label = RichTextLabel.new()
	_step_label.bbcode_enabled = true
	_step_label.fit_content = true
	_step_label.scroll_active = false
	_step_label.autowrap_mode = TextServer.AUTOWRAP_OFF
	_step_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_step_label.custom_minimum_size = Vector2(0, WizardTheme.px(20))
	_step_label.add_theme_font_size_override("normal_font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	_step_label.add_theme_font_size_override("bold_font_size", WizardTheme.fs(WizardTheme.FONT_SIZE_BODY))
	add_child(_step_label)

	# Row 2 : thin progress bar ------------------------------------------
	_progress = ProgressBar.new()
	_progress.show_percentage = false
	_progress.min_value = 0
	_progress.max_value = 100
	_progress.value = 0
	_progress.custom_minimum_size = Vector2(0, WizardTheme.px(3))
	_progress.size_flags_horizontal = Control.SIZE_EXPAND_FILL

	var bg := WizardTheme.make_flat_style(WizardTheme.COLOR_BORDER, 2)
	var fill := WizardTheme.make_flat_style(WizardTheme.COLOR_PRIMARY, 2)
	_progress.add_theme_stylebox_override("background", bg)
	_progress.add_theme_stylebox_override("fill", fill)
	add_child(_progress)

	# Row 3 : separator ---------------------------------------------------
	var sep := HSeparator.new()
	var sep_style := WizardTheme.make_flat_style(WizardTheme.COLOR_BORDER, 0)
	sep_style.content_margin_top = 0
	sep_style.content_margin_bottom = 0
	sep.add_theme_stylebox_override("separator", sep_style)
	sep.add_theme_constant_override("separation", 1)
	add_child(sep)