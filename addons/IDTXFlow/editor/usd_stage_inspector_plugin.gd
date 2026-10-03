@tool
extends EditorInspectorPlugin

## Custom Inspector plugin for UsdStageNode3D (experimental).
##
## Adds a "Reload" button next to `stage_uri` that calls the node's bound
## `reload()` — a fresh re-download + rebuild regardless of whether the URI
## changed (re-setting an unchanged `stage_uri` is a no-op in the setter).
##
## Works for any UsdStageNode3D — a plain local/HTTP stage node or one backed by
## a collaboration session. For a server stage the fetch still relies on the
## auth/session context the Import wizard established (bearer token); the button
## does not set that up, it only re-triggers the load.
##
## WARNING — not safe during an ACTIVE collaboration session. reload() drops the
## stage and its (in-memory) session layer and re-downloads only the committed
## root USD; uncommitted in-session edits (local and peers') are NOT reapplied on
## rebuild, so the local view desyncs from the live session until a fresh edit or
## a rejoin. Use only outside a session (or commit first). See ENH-18.


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

	var reload_btn := Button.new()
	reload_btn.text = "Reload"
	reload_btn.tooltip_text = "Re-download and rebuild this stage, bypassing cached data. Not safe during an active collaboration session: uncommitted in-session edits are not reapplied."
	reload_btn.pressed.connect(func() -> void:
		# Commit any pending edit to the URI first, then force the reload.
		object.set("stage_uri", line_edit.text)
		if object.has_method("reload"):
			object.call("reload")
	)
	hbox.add_child(reload_btn)

	add_property_editor(name, hbox)
	return true
