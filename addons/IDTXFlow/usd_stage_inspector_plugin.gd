@tool
extends EditorInspectorPlugin

## Custom Inspector plugin for UsdStageNode3D.
##
## Purpose: give the user an explicit "Connect" button next to the
## `stage_uri` property. Setting `stage_uri` is what triggers the actual
## USD import, but Godot's default Inspector only re-calls the setter
## when the value *changes*. Users who want to retry / reload with the
## same URI (server was offline, credentials just fixed, etc.) currently
## have no way to force that.
##
## KNOWN LIMITATION: the "Connect" button below is currently a no-op when the
## URI is unchanged. It re-applies the current `stage_uri` value, but
## UsdStageNode3D::set_stage_uri early-returns on an unchanged value
## (`if (stage_uri_ == path) return;`), so no reload happens. A load is only
## triggered when the text is actually edited — the same behavior the default
## Inspector already provides.
##
## PROPOSED FIX: add an explicit reload entry point on the native node — a bound
## `reload()` (or `reopen_stage()`) method that re-runs the open/convert path
## regardless of the current value — and have the "Connect" button call
## `object.call("reload")` instead of re-setting `stage_uri`. Relaxing the
## setter's equality guard is the inferior alternative, since it would also make
## redundant editor writes reload.


func _can_handle(object) -> bool:
	return object != null and object.get_class() == "UsdStageNode3D"


func _parse_property(object, type, name, hint_type, hint_string, usage_flags, wide) -> bool:
	if name != "stage_uri":
		return false

	var hbox := HBoxContainer.new()

	# URI editor
	var line_edit := LineEdit.new()
	line_edit.text = str(object.get("stage_uri"))
	line_edit.placeholder_text = "https://example.com/file.usd"
	line_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line_edit.text_changed.connect(func(new_text: String) -> void:
		object.set("stage_uri", new_text)
	)
	hbox.add_child(line_edit)

	# "Connect" button: intended to re-apply the current URI to force a reload.
	# NOTE: this is a no-op while the URI is unchanged — set_stage_uri ignores an
	# unchanged value. Point this at a native reload() method (see header) to make
	# reload/retry actually work.
	var connect_btn := Button.new()
	connect_btn.text = "Connect"
	connect_btn.tooltip_text = "Re-apply the current URI to reload the stage."
	connect_btn.pressed.connect(func() -> void:
		object.set("stage_uri", line_edit.text)
	)
	hbox.add_child(connect_btn)

	add_property_editor(name, hbox)
	return true
