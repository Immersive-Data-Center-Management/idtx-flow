@tool
extends EditorInspectorPlugin

## Custom Inspector plugin for UsdStageNode3D (experimental).
##
## Adds a "Connect" button next to the `stage_uri` property. Setting `stage_uri`
## is what triggers the USD import, and Godot's Inspector only re-runs the setter
## when the value changes, so this button re-applies the current URI to (re)load.
##
## Known limitation: when the URI is unchanged the button is a no-op —
## UsdStageNode3D::set_stage_uri early-returns on an equal value, so there is no
## forced reload/retry. A true reload/retry for a server stage would also need
## the authenticated login + session context that the Import wizard sets up
## (bearer token / collaboration session), which this standalone inspector does
## not have, so it is intentionally left out here.
##
## Possible future direction: expose an explicit reload entry point on the native
## node — a bound reload() (or reopen_stage()) that re-runs the open/convert path
## regardless of the current value — and have "Connect" call object.call("reload")
## instead of re-setting stage_uri (relaxing the setter's equality guard is worse,
## as it would also reload on redundant editor writes). For a server stage this
## reload path would still need the importer's auth + session context.


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

	# "Connect" re-applies the current URI. No-op while the URI is unchanged
	# (set_stage_uri ignores an equal value); see the header for why a forced
	# server reload is out of scope here.
	var connect_btn := Button.new()
	connect_btn.text = "Connect"
	connect_btn.tooltip_text = "Re-applies the URI; reloads only when it changed."
	connect_btn.pressed.connect(func() -> void:
		object.set("stage_uri", line_edit.text)
	)
	hbox.add_child(connect_btn)

	add_property_editor(name, hbox)
	return true
