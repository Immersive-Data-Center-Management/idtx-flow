@tool
extends VBoxContainer

## Reusable wizard header: progress line + step label.
##
## Layout:
##   Row 1 : "Step N of M — " + <bold_label> + <normal_label>
##   Row 2 : thin (3 px) ProgressBar (uses the editor theme's ProgressBar style)
##   Row 3 : subtle HSeparator (editor theme)

const WizardTheme := preload("res://addons/IDTXFlow/import_manager/wizard_theme.gd")

var _step_label: RichTextLabel
var _progress: ProgressBar


func _init() -> void:
	add_theme_constant_override("separation", WizardTheme.px(6))


func setup(current_step: int, total_steps: int, bold_label: String, normal_label: String) -> void:
	_ensure_built()
	# Use BBCode with only the bold marker; color comes from the editor theme.
	var text := "Step %d of %d — [b]%s[/b] %s" % [current_step, total_steps, bold_label, normal_label]
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
	add_child(_step_label)

	# Row 2 : thin progress bar ------------------------------------------
	_progress = ProgressBar.new()
	_progress.show_percentage = false
	_progress.min_value = 0
	_progress.max_value = 100
	_progress.value = 0
	_progress.custom_minimum_size = Vector2(0, WizardTheme.px(3))
	_progress.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_child(_progress)

	# Row 3 : separator (editor theme) -----------------------------------
	var sep := HSeparator.new()
	add_child(sep)