@tool
extends EditorInspectorPlugin

func _can_handle(object):
	return object.get_class() == "UsdStageNode3D"

func _parse_property(object, type, name, hint_type, hint_string, usage_flags, wide):
	if name == "stage_uri":
		var hbox = HBoxContainer.new()
		
		# Field Text for the Url
		var line_edit = LineEdit.new()
		line_edit.text = object.get_stage_uri()
		line_edit.placeholder_text = "https://example.com/file.usd"
		line_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line_edit.text_changed.connect(func(new_text): object.set_stage_uri(new_text))
		
		# "Connect" Button
		var connect_btn = Button.new()
		connect_btn.text = "Connect"
		connect_btn.pressed.connect(func(): object.set_stage_uri(line_edit.text))
		
		hbox.add_child(line_edit)
		hbox.add_child(connect_btn)
		
		add_property_editor(name, hbox)
		
		return true
	
	return false