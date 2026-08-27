@tool
extends EditorScript

## Dev utility (not part of the shipped runtime): fires the outbound transform path
## exactly once (Godot editor -> server -> other client) to exercise a live move.
##
## Why an EditorScript with a single synchronous call:
##   The converted USD prims are not exposed in the Scene-tree dock (only the
##   UsdStageNode3D root), so there is nothing to mouse-drag. This script reproduces
##   a real gizmo drag deterministically: it sets the converted node's transform (the
##   genuine state change a drag makes) and then calls
##   `IdtxClient.notify_local_transform_changed(node)` — the exact production hook
##   `NOTIFICATION_TRANSFORM_CHANGED` fires on the node. Setting the transform should
##   auto-fire that notification, but editor delivery of transform notifications is
##   unreliable, so the hook is called explicitly as a guaranteed fallback (it reads
##   the node's just-set transform and routes through `CollabEngine::notify_local_edit`
##   — the same gate/TfNotice path as a live drag). No loop, no await, no _process.
##   Arming is handled in C++ (auto-arm via the frame ticker), so a single move is
##   enough to broadcast.
##
## How to use:
##   1. Import a live collaborative_edit stage via the wizard and keep the scene open.
##      The wizard prints the session id + a `watch --sid <id>` command; run that in a
##      terminal to observe broadcasts for the session.
##   2. Choose what to move: leave TARGET_PRIM empty to auto-pick a leaf, or set it to
##      one of the prim paths this script prints; set YAW_DEG / OFFSET for the move.
##   3. Run this script (File -> Run). It resolves the prim, moves the node, and fires
##      the transform-changed hook; `watch` and the server log then show the broadcast.

## The prim to move. Empty auto-picks the DEEPEST tracked prim (a leaf, not the
## stage-root container); set it to one of the prim paths this script prints to
## target a specific prim, e.g. "/World/Sphere" or "/root/Foo/Bar" (a full USD
## path; there is no fixed root name — use what your stage actually uses). NOTE:
## the server only broadcasts changes to tracked LEAF prims, so moving a parent
## group resyncs nothing and produces no broadcast.
const TARGET_PRIM: String = ""

## The move to apply — the operator's choice. A visible, unambiguous transform
## (yaw + a small translation) makes the broadcast and any second-client apply easy
## to eyeball.
const YAW_DEG: float = 45.0
const OFFSET := Vector3(0.5, 0.0, 0.0)


func _run() -> void:
	var client := _idtx()
	if client == null:
		push_error("[emit_outbound_once] IdtxClient singleton unavailable — is the extension DLL loaded?")
		return

	var scene := get_scene()
	if scene == null:
		push_error("[emit_outbound_once] No open scene. Import a server stage first, then run this.")
		return

	# Always show what the scene actually exposes (helps re-target if needed).
	print("[emit_outbound_once] converted prims in this scene:")
	_list_prims(scene)

	var prim_path := TARGET_PRIM
	var node := _resolve_prim(scene, prim_path)
	if node == null:
		if TARGET_PRIM.is_empty():
			push_warning("[emit_outbound_once] No tracked leaf prim found (see list above).")
		else:
			push_warning("[emit_outbound_once] '%s' not found — set TARGET_PRIM to one of the paths above (or \"\" to auto-pick a leaf)." % TARGET_PRIM)
		return
	prim_path = String(node.get_prim_path())

	# Best-effort socket check (clear message rather than a silent no-op).
	if client.has_method("is_socket_open") and not client.is_socket_open():
		push_warning("[emit_outbound_once] Session socket is NOT open — import a live collaborative_edit stage first (enable 'Import as collaboration session' in step 3 and pick Collaborative edit). Sending anyway; it will author locally but won't broadcast.")

	# Build a fresh transform from the current one so the move is visible.
	var t: Transform3D = node.get_transform()
	t.basis = t.basis.rotated(Vector3.UP, deg_to_rad(YAW_DEG))
	t.origin += OFFSET

	# Set the node's transform — the genuine state change a gizmo drag makes.
	# This alone SHOULD auto-fire NOTIFICATION_TRANSFORM_CHANGED (the node has
	# set_notify_transform(true) from _ready()), which the shipped
	# _notification() forwards to notify_local_transform_changed.
	node.set_transform(t)

	# Call the production hook explicitly as a guaranteed fallback, because
	# editor delivery of transform notifications is unreliable. This is the SAME
	# entry point the live gizmo-drag hook uses; it reads the node's just-set
	# transform and routes through CollabEngine::notify_local_edit (gate +
	# TfNotice + coalesce). No await/_process/loop.
	if client.has_method("notify_local_transform_changed"):
		client.notify_local_transform_changed(node)
		print("[emit_outbound_once] moved '%s' and fired notify_local_transform_changed — check `watch` + the server log (OnMessage binary=true / broadcasting)." % prim_path)
	else:
		push_warning("[emit_outbound_once] client lacks notify_local_transform_changed — relying on the node's auto-notification only.")


func _idtx() -> Object:
	if not Engine.has_singleton("IdtxClient"):
		return null
	return Engine.get_singleton("IdtxClient")


## Resolve the target converted USD node (meta __iusdnode3d_ptr, NON-EMPTY prim_path).
## Exact match when `prim` is set; otherwise auto-pick the DEEPEST tracked prim (most
## path segments) so we land on a leaf, never the stage-root container. (The stage root
## also carries the meta but has an empty prim_path, which the bridge's build_index()
## skips too — an empty path would make the move a no-op.)
func _resolve_prim(root: Node, prim: String) -> Node3D:
	if not prim.is_empty():
		return _find_exact(root, prim)
	var best: Node3D = null
	var best_depth := -1
	var stack: Array = [root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		if n.has_meta("__iusdnode3d_ptr") and n is Node3D and n.has_method("get_prim_path"):
			var pp := String(n.get_prim_path())
			if not pp.is_empty():
				var depth := pp.count("/")
				if depth > best_depth:
					best_depth = depth
					best = n
		for c in n.get_children():
			stack.push_back(c)
	return best


func _find_exact(n: Node, prim: String) -> Node3D:
	if n.has_meta("__iusdnode3d_ptr") and n is Node3D and n.has_method("get_prim_path"):
		if String(n.get_prim_path()) == prim:
			return n
	for c in n.get_children():
		var found := _find_exact(c, prim)
		if found:
			return found
	return null


## Print every converted prim's NON-EMPTY path (and node name) so a real target
## can be chosen. Nodes with an empty prim_path (e.g. the stage root) are skipped —
## they are not broadcastable and the bridge's index skips them too.
func _list_prims(n: Node) -> void:
	if n.has_meta("__iusdnode3d_ptr") and n.has_method("get_prim_path"):
		var pp := String(n.get_prim_path())
		if not pp.is_empty():
			print("  prim_path = %s   (node: %s)" % [pp, n.name])
	for c in n.get_children():
		_list_prims(c)
