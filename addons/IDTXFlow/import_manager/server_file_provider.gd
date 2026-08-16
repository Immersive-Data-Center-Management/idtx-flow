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
	if not Engine.has_singleton("IdtxClient"):
		return null
	return Engine.get_singleton("IdtxClient")


func list_dir(dir: String) -> void:
	var client := _idtx()
	if client == null:
		list_failed.emit(dir, "IDTX client not available (GDExtension not loaded).")
		return

	if _loading:
		return
	_loading = true
	_pending_dir = dir

	client.files_listed.connect(_on_files_listed, CONNECT_ONE_SHOT)
	client.request_failed.connect(_on_request_failed, CONNECT_ONE_SHOT)
	client.list_files("", "")


func _on_files_listed(files: Array) -> void:
	_loading = false
	_disconnect_handlers()
	entries_ready.emit(_pending_dir, _build_entries(files))


func _on_request_failed(op: String, http_code: int, code: String, message: String) -> void:
	# Only react to list_files failures here.
	if op != "list_files":
		return
	_loading = false
	_disconnect_handlers()
	var msg := message
	if msg.is_empty():
		msg = "%d %s" % [http_code, code]
	list_failed.emit(_pending_dir, "Failed to list files: %s" % msg)


func _disconnect_handlers() -> void:
	var client := _idtx()
	if client == null:
		return
	if client.files_listed.is_connected(_on_files_listed):
		client.files_listed.disconnect(_on_files_listed)
	if client.request_failed.is_connected(_on_request_failed):
		client.request_failed.disconnect(_on_request_failed)


## Turn the backend's flat file list into browser entries: for each directory
## group, a non-selectable header entry followed by its file entries. The
## browser handles sorting/filtering, so we emit a stable grouped order and let
## it take over from there.
func _build_entries(files: Array) -> Array:
	var entries: Array = []

	# Group entries by their 'directory' field. The backend may return Windows
	# separators (e.g. "Teapot\geo"); normalize to forward slashes for display
	# and for the paths we send back to /sessions and /download.
	var groups := {}   # directory -> Array of entry dicts
	for f in files:
		if typeof(f) != TYPE_DICTIONARY:
			continue
		var directory := String(f.get("directory", "")).replace("\\", "/")
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
			# Normalize Windows separators and any stray leading slash so the
			# path matches the backend's /sessions + /download contract.
			var filepath := String(f.get("filepath", "")).replace("\\", "/").lstrip("/")
			var meta := {
				"name": filename,
				"path": filepath,             # used by get_selected_path()
				"is_dir": false,
				"size_bytes": int(f.get("size", 0)),
				"modified": _format_modified(f.get("modified", 0)),
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


## The backend `modified` is an opaque numeric timestamp
## (file_time_type::time_since_epoch().count()). We can't reliably interpret its
## unit, so present a readable date when the value plausibly looks like Unix
## seconds/millis, otherwise just stringify it. (Used for display only.)
func _format_modified(value) -> String:
	var n := int(value)
	if n <= 0:
		return ""
	var secs := n
	# Heuristic: values far larger than "now in seconds" are millis/nanos.
	if secs > 100_000_000_000:          # > ~year 5138 in seconds → likely millis
		secs = int(secs / 1000)
	if secs > 100_000_000_000:          # still huge → likely micros
		secs = int(secs / 1000)
	if secs > 100_000_000_000:          # still huge → likely nanos
		secs = int(secs / 1000)
	if secs > 1_000_000_000 and secs < 100_000_000_000:
		return Time.get_datetime_string_from_unix_time(secs).substr(0, 10)
	# Fallback: opaque value, show as-is.
	return str(value)