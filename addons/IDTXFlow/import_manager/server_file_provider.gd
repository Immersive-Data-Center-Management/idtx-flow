@tool
extends "res://addons/IDTXFlow/import_manager/file_provider.gd"

## IDTX asset-server data source for `WizardFileBrowser`.
##
## Backed by the real IDTX backend via the `IdtxClient` engine singleton
## (GET /api/v1/files). The backend returns a single FLAT, recursive listing of
## every USD file (each with `filepath` + `directory`). From that one response we
## build an in-memory folder tree and serve one directory level per `list_dir()`,
## so the browser can navigate folders exactly like the local filesystem — with
## no per-folder network calls and no backend subtree endpoint.
##
## Path model:
##   • Folder navigation paths are rooted at the server URL (the browser's
##     `_root_prefix`)
##   • File entries keep the backend-relative `filepath` as their `path`/`meta.path`, because that is
##     what session-create / download / thumbnail all consume.
##
## The tree is built once and reused; the refresh button calls `request_reload()`
## to discard it and re-fetch (so new uploads appear).

const IdtxAccess := preload("res://addons/IDTXFlow/import_manager/idtx_client_access.gd")

var _server_url: String = ""
var _loading: bool = false
## The directory currently being listed (browser-rooted); echoed back with the
## results so the browser can ignore stale responses.
var _pending_dir: String = ""

## Folder tree built from the flat listing: rel_dir -> { "subdirs": {name:true},
## "files": [file-entry dict, ...] }. rel_dir is backend-relative; "" is root.
var _tree: Dictionary = {}
var _loaded: bool = false


func set_server_url(url: String) -> void:
	_server_url = url


func get_root_prefix() -> String:
	return _server_url


func supports_navigation() -> bool:
	return true


func dir_exists(dir: String) -> bool:
	if dir == _server_url or dir.is_empty():
		return true
	if not _loaded:
		# Before the tree is built we can't validate; allow the root only.
		return _rel_of(dir) == ""
	return _tree.has(_rel_of(dir))


func request_reload() -> void:
	# Discard the cached tree; the next list_dir() re-fetches and rebuilds.
	_tree = {}
	_loaded = false


func _idtx() -> Object:
	return IdtxAccess.get_client()


## Convert a browser-rooted directory path into a backend-relative directory
## ("" for the root). Strips the server-URL prefix and surrounding slashes.
func _rel_of(browser_dir: String) -> String:
	var rel := browser_dir
	if not _server_url.is_empty() and rel.begins_with(_server_url):
		rel = rel.substr(_server_url.length())
	rel = rel.strip_edges()
	while rel.begins_with("/"):
		rel = rel.substr(1)
	while rel.ends_with("/"):
		rel = rel.substr(0, rel.length() - 1)
	return rel


## Convert a backend-relative directory into a browser-rooted navigation path.
func _browser_path(rel: String) -> String:
	if rel.is_empty():
		return _server_url
	return _server_url + "/" + rel


func list_dir(dir: String) -> void:
	var client := _idtx()
	if client == null:
		list_failed.emit(dir, "IDTX client not available (GDExtension not loaded).")
		return

	_pending_dir = dir

	# Serve navigation from the already-built tree (no network round-trip).
	if _loaded:
		entries_ready.emit(dir, _level_entries(_rel_of(dir)))
		return

	if _loading:
		return
	_loading = true
	client.list_files("", "", _on_list_done)


func _on_list_done(result: Dictionary) -> void:
	_loading = false
	if not bool(result.get("ok", false)):
		var msg := String(result.get("message", ""))
		if msg.is_empty():
			msg = "%d %s" % [int(result.get("http_code", 0)), String(result.get("error_code", ""))]
		list_failed.emit(_pending_dir, "Failed to list files: %s" % msg)
		return
	_build_tree(result.get("result", []))
	_loaded = true
	entries_ready.emit(_pending_dir, _level_entries(_rel_of(_pending_dir)))


func supports_thumbnails() -> bool:
	return true


## Fetch a thumbnail for a server file via IdtxClient. The core caches the bytes
## by usd_file, so repeat requests (re-render/re-list) don't re-hit the network.
func request_thumbnail(usd_file: String) -> void:
	var client := _idtx()
	if client == null or not client.has_method("fetch_thumbnail"):
		return
	# /thumbnail is authenticated; skip when not logged in (avoids a 401).
	if client.has_method("is_authenticated") and not client.is_authenticated():
		return
	client.fetch_thumbnail(usd_file, _on_thumb_done_img.bind(usd_file))


func _on_thumb_done_img(result: Dictionary, usd_file: String) -> void:
	if not bool(result.get("ok", false)):
		return   # silent: caller keeps its placeholder icon
	var data: Dictionary = result.get("result", {})
	var bytes: PackedByteArray = data.get("bytes", PackedByteArray())
	if bytes.is_empty():
		return
	thumbnail_ready.emit(usd_file, bytes, String(data.get("content_type", "")))


## Build the folder tree from the backend's flat listing. Every file's
## `directory` contributes its full ancestor chain (so intermediate folders that
## contain only subfolders still appear), and the file is bucketed under its own
## directory.
func _build_tree(files: Array) -> void:
	_tree = {}
	_ensure_dir("")   # root always exists
	for f in files:
		if typeof(f) != TYPE_DICTIONARY:
			continue
		var directory := String(f.get("directory", "")).strip_edges()
		# Normalize slashes and trim.
		while directory.begins_with("/"):
			directory = directory.substr(1)
		while directory.ends_with("/"):
			directory = directory.substr(0, directory.length() - 1)

		# Register every ancestor directory and link each to its parent.
		_register_dir_chain(directory)

		# Bucket the file under its directory.
		_ensure_dir(directory)
		_tree[directory]["files"].append({
			"filename": String(f.get("filename", "")),
			"filepath": String(f.get("filepath", "")),
			"size": int(f.get("size", 0)),
			"modified": int(f.get("modified", 0)),
			"modified_epoch": int(f.get("modified_epoch", 0)),
		})


func _ensure_dir(rel: String) -> void:
	if not _tree.has(rel):
		_tree[rel] = {"subdirs": {}, "files": []}


## Ensure `rel` and all its ancestors exist, and record each level's child in its
## parent's `subdirs` set.
func _register_dir_chain(rel: String) -> void:
	if rel.is_empty():
		return
	var parts := rel.split("/", false)
	var accum := ""
	for part in parts:
		var parent := accum
		accum = part if accum.is_empty() else (accum + "/" + part)
		_ensure_dir(parent)
		_ensure_dir(accum)
		_tree[parent]["subdirs"][part] = true


## Build the browser entries for a single directory level: navigable subfolders
## first, then the files directly in that directory.
func _level_entries(rel: String) -> Array:
	var entries: Array = []
	if not _tree.has(rel):
		return entries

	var node: Dictionary = _tree[rel]

	# Subdirectory entries (navigable). Names sorted alphabetically.
	var subdir_names: Array = node["subdirs"].keys()
	subdir_names.sort()
	for sub in subdir_names:
		var child_rel: String = sub if rel.is_empty() else (rel + "/" + sub)
		entries.append({
			"name": String(sub),
			"is_dir": true,
			"path": _browser_path(child_rel),   # browser-rooted for navigation
			"selectable": true,
			"meta": {},
		})

	# File entries in this directory, sorted by filename.
	var files: Array = node["files"].duplicate()
	files.sort_custom(func(a, b):
		return String(a.get("filename", "")) < String(b.get("filename", "")))
	for f in files:
		var filename := String(f.get("filename", ""))
		var filepath := String(f.get("filepath", ""))   # backend-relative (import/download contract)
		var meta := {
			"name": filename,
			"path": filepath,
			"is_dir": false,
			"size_bytes": int(f.get("size", 0)),
			"modified": _format_modified(f),
			"description": rel,
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