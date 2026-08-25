@tool
extends "res://addons/IDTXFlow/import_manager/file_provider.gd"

## IDTX asset-server data source for `WizardFileBrowser`.
##
## Backed by the real IDTX backend via the `IdtxClient` engine singleton
## (GET /api/v1/files). The backend returns a flat file list; we present it
## grouped by the `directory` field: a non-selectable directory header entry
## followed by its files.
##
## Listing is asynchronous — `list_dir()` kicks off `IdtxClient.list_files()`
## and resolves via `entries_ready` on `files_listed` or `list_failed` on
## `request_failed`.
##
## `supports_navigation()` is false for now (flat, grouped list). When the
## backend gains subtree listing, flip it to true and have `list_dir(dir)`
## request that subtree — the browser UI already supports navigation.

const IdtxAccess := preload("res://addons/IDTXFlow/import_manager/idtx_client_access.gd")

var _server_url: String = ""
var _loading: bool = false
## The directory currently being listed; echoed back with the results so the
## browser can ignore stale responses.
var _pending_dir: String = ""


func set_server_url(url: String) -> void:
	_server_url = url


func get_root_prefix() -> String:
	return _server_url


func supports_navigation() -> bool:
	return false


func dir_exists(dir: String) -> bool:
	# Flat listing: only the root "exists" as far as navigation is concerned.
	return dir == _server_url or dir.is_empty()


func _idtx() -> Object:
	return IdtxAccess.get_client()


func list_dir(dir: String) -> void:
	var client := _idtx()
	if client == null:
		list_failed.emit(dir, "IDTX client not available (GDExtension not loaded).")
		return

	if _loading:
		return
	_loading = true
	_pending_dir = dir

	client.list_files("", "", _on_list_done)


func _on_list_done(result: Dictionary) -> void:
	_loading = false
	if bool(result.get("ok", false)):
		entries_ready.emit(_pending_dir, _build_entries(result.get("result", [])))
		return
	var msg := String(result.get("message", ""))
	if msg.is_empty():
		msg = "%d %s" % [int(result.get("http_code", 0)), String(result.get("error_code", ""))]
	list_failed.emit(_pending_dir, "Failed to list files: %s" % msg)


## Turn the backend's flat file list into browser entries: for each directory
## group, a non-selectable header entry followed by its file entries. The
## browser handles sorting/filtering, so we emit a stable grouped order and let
## it take over from there.
func _build_entries(files: Array) -> Array:
	var entries: Array = []

	# Group entries by their 'directory' field. Paths arrive already normalized
	# to forward slashes, so grouping is a pure display concern.
	var groups := {}   # directory -> Array of entry dicts
	for f in files:
		if typeof(f) != TYPE_DICTIONARY:
			continue
		var directory := String(f.get("directory", ""))
		if not groups.has(directory):
			groups[directory] = []
		groups[directory].append(f)

	# Sort directory names ("" root first, then alphabetical).
	var dir_names := groups.keys()
	dir_names.sort_custom(func(a, b):
		if a == "":
			return true
		if b == "":
			return false
		return String(a) < String(b))

	for directory in dir_names:
		# Directory header entry (non-selectable).
		var header_text: String = "/" if String(directory).is_empty() else (String(directory) + "/")
		entries.append({
			"name": header_text,
			"is_dir": true,
			"path": "",
			"selectable": false,
			"meta": {},
		})

		# File entries under this directory, sorted by filename.
		var group_files: Array = groups[directory]
		group_files.sort_custom(func(a, b):
			return String(a.get("filename", "")) < String(b.get("filename", "")))

		for f in group_files:
			var filename := String(f.get("filename", ""))
			# The path already matches the /sessions + /download contract.
			var filepath := String(f.get("filepath", ""))
			var meta := {
				"name": filename,
				"path": filepath,             # used by get_selected_path()
				"is_dir": false,
				"size_bytes": int(f.get("size", 0)),
				"modified": _format_modified(f),
				"description": String(directory),
			}
			entries.append({
				"name": filename,
				"is_dir": false,
				"path": filepath,
				"selectable": true,
				"meta": meta,
			})

	return entries


## Present the modification time for display. The engine decodes the opaque
## backend value to Unix seconds (`modified_epoch`, 0 when it cannot be
## interpreted); show a readable date when it resolves, otherwise the raw value.
func _format_modified(entry) -> String:
	var epoch := int(entry.get("modified_epoch", 0))
	if epoch > 0:
		return Time.get_datetime_string_from_unix_time(epoch).substr(0, 10)
	var raw := int(entry.get("modified", 0))
	if raw <= 0:
		return ""
	return str(raw)