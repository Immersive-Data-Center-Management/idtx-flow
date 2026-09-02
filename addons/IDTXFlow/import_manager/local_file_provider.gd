@tool
extends "res://addons/IDTXFlow/import_manager/file_provider.gd"

## Local-filesystem data source for `WizardFileBrowser`.
##
## Wraps `DirAccess`/`FileAccess` and mirrors `FileDialog`'s access model
## (res:// / user:// / OS filesystem). Listing is synchronous — `list_dir()`
## reads the directory immediately and emits `entries_ready` in the same frame.
##
## The browser applies filters, filename filter and sorting itself; this
## provider only returns the raw directory/file entries plus per-file modified
## timestamps in `meta` so the browser can sort by modified time.

enum Access { ACCESS_RESOURCES, ACCESS_USERDATA, ACCESS_FILESYSTEM }

var _access: int = Access.ACCESS_RESOURCES
var _root_prefix: String = "res://"
var _show_hidden: bool = false


func set_access(access: int) -> void:
	_access = access
	match access:
		Access.ACCESS_RESOURCES:
			_root_prefix = "res://"
		Access.ACCESS_USERDATA:
			_root_prefix = "user://"
		Access.ACCESS_FILESYSTEM:
			_root_prefix = ""


func get_access() -> int:
	return _access


func set_show_hidden(show: bool) -> void:
	_show_hidden = show


func get_root_prefix() -> String:
	return _root_prefix


func supports_navigation() -> bool:
	return true


func dir_exists(dir: String) -> bool:
	return DirAccess.dir_exists_absolute(dir)


func list_dir(dir: String) -> void:
	var entries: Array = []

	var d := DirAccess.open(dir)
	if d == null:
		list_failed.emit(dir, "Could not open directory: %s" % dir)
		return

	d.include_navigational = false
	d.include_hidden = _show_hidden

	d.list_dir_begin()
	var name := d.get_next()
	while name != "":
		if _show_hidden or not name.begins_with("."):
			var is_dir := d.current_is_dir()
			var full := _join(dir, name)
			var meta := {}
			if not is_dir:
				# Expose modified time so the browser can sort by it without
				# re-touching the filesystem.
				meta["modified_unix"] = FileAccess.get_modified_time(full)
			entries.append({
				"name": name,
				"is_dir": is_dir,
				"path": full,
				"selectable": true,
				"meta": meta,
			})
		name = d.get_next()
	d.list_dir_end()

	entries_ready.emit(dir, entries)


func _join(base: String, name: String) -> String:
	if base.is_empty():
		return name
	if base.ends_with("/"):
		return base + name
	return base + "/" + name