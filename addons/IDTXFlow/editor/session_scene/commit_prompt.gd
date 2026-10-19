@tool
extends RefCounted

## Editor commit-prompt dialog.
##
## Owns a single reusable ConfirmationDialog asking whether to persist a session's
## edits to the server (Commit) or skip (Cancel). It governs only the server commit;
## the local scene is already saved by the time the prompt appears, so Cancel is a
## safe no-op. Editor-tier UI tooling: the parent base control is injected via `setup`.

var _dialog: ConfirmationDialog = null
# Reset whenever the dialog closes so a stale callable can never fire on a later save.
var _on_commit: Callable = Callable()


## Build the dialog once and parent it under the editor base control. Idempotent.
func setup(base_control: Control) -> void:
	if _dialog != null or base_control == null:
		return
	_dialog = ConfirmationDialog.new()
	_dialog.title = "IDTXFlow — Commit Session"
	_dialog.set_ok_button_text("Commit")
	_dialog.get_cancel_button().text = "Cancel"
	_dialog.confirmed.connect(_on_confirmed)
	_dialog.canceled.connect(_on_canceled)
	base_control.add_child(_dialog)


## Prompt to commit `session_id`, running `on_commit` if the user confirms.
## Ignored while a prompt is already visible so a mid-dialog save can't stack dialogs.
func prompt(session_id: String, on_commit: Callable) -> void:
	if _dialog == null or _dialog.visible:
		return
	_on_commit = on_commit
	_dialog.dialog_text = "Commit edits for session '%s' to the server?" % session_id
	_dialog.popup_centered()


func _on_confirmed() -> void:
	var cb := _on_commit
	_on_commit = Callable()
	if cb.is_valid():
		cb.call()


func _on_canceled() -> void:
	_on_commit = Callable()


## Free the dialog on plugin teardown.
func clear() -> void:
	if _dialog != null:
		_dialog.queue_free()
		_dialog = null
	_on_commit = Callable()
