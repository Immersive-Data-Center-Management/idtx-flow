@tool
extends EditorExportPlugin

## Makes sure exported games contain the USD files their scenes need.
##
## When Godot exports a game, it packs the scenes and the files they reference into a
## package (.pck). It finds those files by following resource links in the scenes. A
## UsdStageNode3D only stores its file as a text path (`stage_uri`), so Godot doesn't see
## the USD file and leaves it out, and the stage fails to load in the exported game.
##
## This plugin runs during export. For every exported scene it reads the `stage_uri` of
## each node and adds that USD file to the package, plus everything the file uses:
## sublayers, references, payloads and textures. Textures are added as the original image
## files, because Godot would otherwise only pack its own converted copies (.ctex), which
## OpenUSD can't read.
##
## Only res:// paths are added. http(s):// stages are downloaded at runtime instead.

var _added := {}


func _get_name() -> String:
	return "IDTXFlowUsdExport"


func _export_begin(_features: PackedStringArray, _is_debug: bool, _path: String, _flags: int) -> void:
	_added.clear()


func _export_file(path: String, type: String, _features: PackedStringArray) -> void:
	if type != "PackedScene":
		return
	for uri in collect_stage_uris(load(path) as PackedScene):
		for file in [uri] + Array(UsdStageNode3D.get_stage_dependencies(uri)):
			_add_raw(file)


## All res:// `stage_uri` values stored in the scene.
static func collect_stage_uris(scene: PackedScene) -> PackedStringArray:
	var uris := PackedStringArray()
	if scene == null:
		return uris
	var state := scene.get_state()
	for i in state.get_node_count():
		for p in state.get_node_property_count(i):
			if state.get_node_property_name(i, p) != &"stage_uri":
				continue
			var uri := str(state.get_node_property_value(i, p))
			if uri.begins_with("res://") and not uris.has(uri):
				uris.push_back(uri)
	return uris


func _add_raw(file: String) -> void:
	if _added.has(file):
		return
	_added[file] = true
	if not FileAccess.file_exists(file):
		push_warning("IDTXFlow export: USD asset not found, skipped: %s" % file)
		return
	add_file(file, FileAccess.get_file_as_bytes(file), false)
