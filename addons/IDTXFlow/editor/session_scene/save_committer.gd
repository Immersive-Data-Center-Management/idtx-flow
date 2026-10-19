@tool
extends RefCounted

## Editor commit-on-save orchestrator.
##
## Bridges the editor save flow to a session commit: on save, if the focused scene
## is a live session scene, it prompts (Commit / Cancel) and asks the coordinator to
## persist that session to the server. Owns the prompt dialog and the commit-result
## reactions, so the plugin only forwards the editor's save callback.
##
## Editor-tier tooling: the editor interface (for the focused scene + dialog parent)
## and the coordinator (for path→session resolve + commit) are injected via `setup`.

const SessionCommitPrompt := preload("res://addons/IDTXFlow/editor/session_scene/commit_prompt.gd")

var _editor_interface: EditorInterface = null
var _coordinator: RefCounted = null
var _prompt: RefCounted = null


## Wire the dependencies and build the prompt. Idempotent.
func setup(editor_interface: EditorInterface, coordinator: RefCounted) -> void:
	if _prompt != null:
		return
	_editor_interface = editor_interface
	_coordinator = coordinator
	_prompt = SessionCommitPrompt.new()
	if _editor_interface != null:
		_prompt.setup(_editor_interface.get_base_control())
	if _coordinator != null:
		_coordinator.session_committed.connect(_on_session_committed)
		_coordinator.session_commit_failed.connect(_on_session_commit_failed)


## Handle an editor save: when the focused scene is a live session scene, prompt to
## commit it. The local scene is already saved, so this governs only the server commit.
func on_editor_save() -> void:
	if _coordinator == null or _prompt == null or _editor_interface == null:
		return
	var root := _editor_interface.get_edited_scene_root()
	if root == null:
		return
	var sid: String = _coordinator.active_session_id_for_path(root.get_scene_file_path())
	if sid.is_empty():
		return
	_prompt.prompt(sid, func() -> void: _coordinator.commit_session(sid))


func _on_session_committed(session_id: String, committed: bool) -> void:
	if committed:
		print("[IDTXFlow] Session '%s' committed to server." % session_id)
	else:
		print("[IDTXFlow] Session commit: nothing to commit.")


func _on_session_commit_failed(message: String) -> void:
	push_warning("[IDTXFlow] Session commit failed: %s" % message)


## Free the prompt on plugin teardown.
func clear() -> void:
	if _prompt != null:
		_prompt.clear()
		_prompt = null
	_editor_interface = null
	_coordinator = null
