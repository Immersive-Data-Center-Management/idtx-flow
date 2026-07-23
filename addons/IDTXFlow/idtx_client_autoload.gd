@tool
extends Node

## Autoload wrapper that puts the native IdtxClient node into the SceneTree.
##   By creating the IdtxClient here and add_child()-ing it, the node lives in
##   the SceneTree and receives _process, so poll() flushes outbound updates.
##
## Runtime/export note:
##   This autoload is registered by plugin.gd via add_autoload_singleton(),
##   which persists an [autoload] entry into project.godot. That entry is read
##   at game startup independently of the (editor-only) EditorPlugin, so the
##   client is created and ticks in exported/packaged games as well as in the
##   editor.

var idtx_client: IdtxClient = null


func _ready() -> void:
	print("[IDTXFlow] IdtxClientAutoload._ready() - creating IdtxClient instance")
	idtx_client = IdtxClient.new()
	idtx_client.name = "IdtxClient"
	add_child(idtx_client)
	# Make the tree-resident instance the global singleton so existing GDScript
	# that resolves `IdtxClient` (the engine singleton name) keeps working and
	# there is exactly one instance in the process.
	if idtx_client.has_method("set_as_singleton"):
		idtx_client.set_as_singleton()


func _exit_tree() -> void:
	print("[IDTXFlow] IdtxClientAutoload._exit_tree() - cleaning up IdtxClient instance")
	if idtx_client:
		remove_child(idtx_client)
		idtx_client.free()
		idtx_client = null