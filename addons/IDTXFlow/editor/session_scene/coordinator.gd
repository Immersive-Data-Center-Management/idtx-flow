@tool
extends RefCounted

## Editor session-scene coordinator.
##
## Owns the editor-only bookkeeping for live collaboration sessions that the
## import wizard triggers: the `session_id -> transient-scene-path` registry, the
## hidden `res://.idtxflow_sessions/*.tscn` file scheme, teardown timing, and the
## `scene_closed` / startup-prune reactions. It *triggers* the runtime client
## (`IdtxClient.end_session`) for leave/teardown, but owns no session *protocol* state (that lives in the engine).
##
## This is strictly editor-tier tooling: it deals with `res://` scenes, the FileSystem dock and editor tabs.
##
## The wizard drives UI and starts imports/sessions, then hands the resulting
## `session_id`/scene path here via `register_session_scene`. `plugin.gd` wires the
## editor's `scene_closed` and startup prune to this coordinator.

const IdtxAccess := preload("res://addons/IDTXFlow/editor/idtx_client_access.gd")

## Hidden folder for transient session scenes. Dot-prefixed so the FileSystem dock
## and the import pipeline ignore it — the files are never treated as project assets.
const SESSION_DIR := "res://.idtxflow_sessions"

# Transient session scenes we created, keyed by session_id -> scene path.
var _session_scenes: Dictionary = {}

## Emitted whenever the set of tracked session scenes changes (create / join /
## close / teardown), so the editor plugin can refresh the "live session"
## indicators (viewport border + tab marker).
signal session_scenes_changed

## Emitted once a session has been acquired (created or joined) and its stage
## download URL resolved. The wizard listens and materializes the stage. The
## `session_id` is also captured here so the transient scene can be tracked.
signal session_stage_ready(session_id: String, stage_url: String)

## Emitted when a create/join request fails (after the server round-trip), with a human-readable message the wizard can surface.
signal session_failed(message: String)

## Emitted after a commit request succeeds. `committed` is false when the server had nothing to persist.
signal session_committed(session_id: String, committed: bool)

## Emitted when a commit request fails for a non-benign reason
signal session_commit_failed(message: String)

func _idtx() -> Object:
	return IdtxAccess.get_client()


# --------------------------------------------------------------------------
# Session acquisition (create / join)
#
# The coordinator triggers acquisition and the duplicate-join guard, and reports
# the resolved stage URL via the per-request `on_done` completion; it does not
# materialize the stage (that is editor import work the wizard performs on `session_stage_ready`).
# --------------------------------------------------------------------------

## Create a new collaboration session for `file_path` with `mode`
## (single_edit / collaborative_edit), then enter it. The per-request `on_done`
## completion carries the resolved stage URL, from which we emit `session_stage_ready`.
func create_session(file_path: String, mode: String, auto_commit: bool = false) -> void:
	var client := _idtx()
	if client == null:
		session_failed.emit("IDTX client not available; cannot start server session.")
		return
	print("[IDTXFlow] [Session Scene] Creating '%s' session." % mode)
	client.open_new_session(file_path, mode, auto_commit, _on_create_done)


## Join a running session by id, then enter it. Refuses a session we are already
## in (client-side duplicate-join guard). Shares the completion handling with create.
func join_session(session_id: String) -> void:
	var client := _idtx()
	if client == null:
		session_failed.emit("IDTX client not available; cannot join session.")
		return
	if session_id.is_empty():
		session_failed.emit("No session selected to join.")
		return
	# Client-side duplicate-join guard: we cannot join a session we are already in.
	if has_session(session_id):
		session_failed.emit("Already in session '%s'; not joining again." % session_id)
		return
	print("[IDTXFlow] [Session Scene] Joining session '%s'." % session_id)
	client.open_existing_session(session_id, _on_join_done)


func _on_create_done(result: Dictionary) -> void:
	_on_acquire_done(result)


func _on_join_done(result: Dictionary) -> void:
	_on_acquire_done(result)


## Shared per-request completion for create/join. On success the result carries the
## session dict (session_id + resolved stage_url); emit `session_stage_ready` so the
## wizard materializes the stage. On failure emit `session_failed`.
func _on_acquire_done(result: Dictionary) -> void:
	if bool(result.get("ok", false)):
		var session: Dictionary = result.get("result", {})
		var sid: String = session.get("session_id", "")
		var stage_url: String = session.get("stage_url", "")
		print("[IDTXFlow] [Session Scene] Session ready: session_id=%s" % sid)
		session_stage_ready.emit(sid, stage_url)
		return
	session_failed.emit("Session acquisition failed (%d %s): %s" % [
		int(result.get("http_code", 0)), String(result.get("error_code", "")),
		String(result.get("message", ""))])



# --------------------------------------------------------------------------
# Registry queries (used by the wizard + indicators)
# --------------------------------------------------------------------------

## Track a transient session scene created by the wizard so it can be cleaned up
## on scene_closed / teardown / editor close. `auto_commit` records whether the
## session was created with server-side auto-commit (shown by the live indicators).
func register_session_scene(session_id: String, path: String, auto_commit: bool = false) -> void:
	if session_id.is_empty():
		return
	_session_scenes[session_id] = { "path": path, "auto_commit": auto_commit }
	session_scenes_changed.emit()


## True when a session id is already tracked (used for the duplicate-join guard).
func has_session(session_id: String) -> bool:
	return _session_scenes.has(session_id)


## The set of currently tracked session ids (used to gray out "joined" rows).
func joined_ids() -> PackedStringArray:
	var ids := PackedStringArray()
	for sid in _session_scenes.keys():
		ids.append(sid)
	return ids


## True when `path` is one of the transient session scenes we created (used by the
## editor plugin to decide whether to show the "live session" indicators).
func is_session_scene_path(path: String) -> bool:
	for sid in _session_scenes:
		if _session_scenes[sid]["path"] == path:
			return true
	return false


## The session id backing the transient scene at `path`, or "" if none.
func active_session_id_for_path(path: String) -> String:
	for sid in _session_scenes:
		if _session_scenes[sid]["path"] == path:
			return sid
	return ""


## Whether the session backing the transient scene at `path` was created with
## auto-commit on. False if the path is not a tracked session scene.
func auto_commit_for_path(path: String) -> bool:
	for sid in _session_scenes:
		if _session_scenes[sid]["path"] == path:
			return _session_scenes[sid]["auto_commit"]
	return false


# --------------------------------------------------------------------------
# Transient-scene path scheme
# --------------------------------------------------------------------------

## Path for a transient session-backed scene. Lives in a hidden
## res://.idtxflow_sessions/ folder (dot-prefixed → ignored by the FileSystem
## dock and the import pipeline) so it is never treated as a project asset; the
## folder is created on demand and removed when the last session scene is gone.
func transient_scene_path(session_id: String, basename: String) -> String:
	DirAccess.make_dir_recursive_absolute(SESSION_DIR)
	var stem: String = session_id
	if stem.is_empty():
		stem = basename if not basename.is_empty() else "UsdSession"
	# Sanitize the id into a filesystem-safe stem.
	stem = stem.validate_filename()
	return "%s/%s.tscn" % [SESSION_DIR, stem]


# --------------------------------------------------------------------------
# Commit (persist the session layer to the file on the server)
#
# Edits already stream to the server live, so a commit is the server-side
# "write it into the file" step. A 409 "nothing_to_commit" is a benign no-op.
# --------------------------------------------------------------------------

## Ask the server to persist one session's edits into its file. No-op when the
## client is unavailable or the id is unknown. Results arrive on `_on_commit_done`.
func commit_session(session_id: String) -> void:
	if session_id.is_empty():
		return
	var client := _idtx()
	if client == null or not client.has_method("commit_session"):
		session_commit_failed.emit("IDTX client not available; cannot commit session.")
		return
	print("[IDTXFlow] [Session Scene] Committing session '%s'." % session_id)
	client.commit_session(session_id, _on_commit_done)


## Per-request commit completion. On success emit `session_committed` (committed
## false = benign server-side no-op). The 409 "nothing_to_commit" error is treated
## as a benign no-op too; any other failure emits `session_commit_failed`.
func _on_commit_done(result: Dictionary) -> void:
	if bool(result.get("ok", false)):
		var r: Dictionary = result.get("result", {})
		var sid: String = r.get("session_id", "")
		var committed: bool = bool(r.get("committed", false))
		print("[IDTXFlow] [Session Scene] Commit done: session_id=%s committed=%s" % [sid, committed])
		session_committed.emit(sid, committed)
		return
	var code := String(result.get("error_code", ""))
	if code == "nothing_to_commit":
		print("[IDTXFlow] [Session Scene] Commit: nothing to commit (no-op).")
		session_committed.emit("", false)
		return
	session_commit_failed.emit("Commit failed (%d %s): %s" % [
		int(result.get("http_code", 0)), code, String(result.get("message", ""))])


# --------------------------------------------------------------------------
# Leave / teardown
# --------------------------------------------------------------------------

## Leave one collaboration session: tell the engine to end that session (detach
## its stage + close its socket) and delete its transient scene. Safe when the id
## is unknown. This is the per-tab "leave" used when a single session scene closes.
func leave_session(session_id: String) -> void:
	if session_id.is_empty():
		return
	var client := _idtx()
	if client and client.has_method("end_session"):
		client.end_session(session_id)
	_cleanup_session_scene(session_id)


## Tear down every active session. Ends each session in the engine and deletes all transient scenes.
func teardown_all_sessions() -> void:
	var client := _idtx()
	for sid in _session_scenes.keys().duplicate():
		if client and client.has_method("end_session"):
			client.end_session(sid)
	_cleanup_all_session_scenes()


## A tracked session scene's editor tab was closed by the user (EditorPlugin
## scene_closed). Treat it as an implicit "leave session": tear the session down
## and delete the transient file. Ignores non-session paths.
func on_session_scene_closed(filepath: String) -> void:
	for sid in _session_scenes.keys().duplicate():
		if _session_scenes[sid]["path"] == filepath:
			print("[IDTXFlow] [Session Scene] Session scene tab closed; leaving session '%s'." % sid)
			leave_session(sid)
			return


## Delete any stray transient session scenes left by a prior crash / hard close
## where scene_closed / _exit_tree never fired. Called once at plugin startup.
func prune_stale_session_scenes() -> void:
	var dir := DirAccess.open(SESSION_DIR)
	if dir == null:
		return
	for f in dir.get_files():
		if f.ends_with(".tscn"):
			DirAccess.remove_absolute("%s/%s" % [SESSION_DIR, f])
	_remove_session_dir_if_empty()


# --------------------------------------------------------------------------
# Internal cleanup
# --------------------------------------------------------------------------

## Delete the transient scene file for a tracked session. The scene may still be
## open as an editor tab; there is no API to programmatically close a specific
## tab, so we delete the file and leave any empty tab for the user to close.
func _cleanup_session_scene(session_id: String) -> void:
	if not _session_scenes.has(session_id):
		return
	var path: String = _session_scenes[session_id]["path"]
	_session_scenes.erase(session_id)
	session_scenes_changed.emit()
	if FileAccess.file_exists(path):
		var err := DirAccess.remove_absolute(path)
		print("[IDTXFlow] [Session Scene] Removed transient session scene '%s' (err=%d)." % [path, err])
	_remove_session_dir_if_empty()


## Delete every tracked transient session scene (teardown / editor close).
func _cleanup_all_session_scenes() -> void:
	for sid in _session_scenes.keys().duplicate():
		_cleanup_session_scene(sid)


## Remove the hidden session-scene directory if it is now empty, so we don't
## leave an empty folder behind in the project.
func _remove_session_dir_if_empty() -> void:
	if not _session_scenes.is_empty():
		return
	var dir := DirAccess.open(SESSION_DIR)
	if dir == null:
		return
	if dir.get_files().is_empty() and dir.get_directories().is_empty():
		DirAccess.remove_absolute(SESSION_DIR)
