@tool
extends RefCounted

## Marks the editor scene tab of a live collaboration session.

const TITLE_PREFIX := "[remote scene] "

var _editor_interface: EditorInterface
var _tabbar: TabBar = null
var _dot_icon: Texture2D = null
# Original titles we overrode, keyed by scene basename, so they can be restored.
var _original_titles: Dictionary = {}
var _warned_missing := false


func setup(editor_interface: EditorInterface) -> void:
	_editor_interface = editor_interface
	_dot_icon = _make_dot_icon()


## Apply markers: for every open scene tab whose path is in `session_paths`, prefix
## the title, set the green dot icon, and a tooltip; restore any tab that is no
## longer a session. `session_paths` is an Array/PackedStringArray of res:// paths.
func refresh(session_paths) -> void:
	var tb := _get_tabbar()
	if tb == null:
		return

	# Tab titles are scene basenames WITHOUT extension (e.g. "0d83f161-…"), and
	# tab order need not match get_open_scenes(). Match each tab by basename against
	# the session paths' basenames.
	var session_basenames := {}
	for p in session_paths:
		session_basenames[String(p).get_file().get_basename()] = true

	for i in tb.get_tab_count():
		var current := tb.get_tab_title(i)
		var base_name := _tab_title_to_basename(current)
		var is_session: bool = session_basenames.has(base_name)
		if is_session:
			if not current.begins_with(TITLE_PREFIX):
				if not _original_titles.has(base_name):
					_original_titles[base_name] = current
				tb.set_tab_title(i, TITLE_PREFIX + current)
			if _dot_icon:
				tb.set_tab_icon(i, _dot_icon)
			tb.set_tab_tooltip(i, "Live IDTXFlow collaboration session")
		else:
			_restore_tab(tb, i, base_name, current)


## Restore all tabs we touched and forget them (teardown / plugin exit).
func clear() -> void:
	var tb := _get_tabbar()
	if tb != null:
		for i in tb.get_tab_count():
			var current := tb.get_tab_title(i)
			_restore_tab(tb, i, _tab_title_to_basename(current), current)
	_original_titles.clear()


# ---------------------------------------------------------------------------
# Internals
# ---------------------------------------------------------------------------

func _restore_tab(tb: TabBar, i: int, base_name: String, current: String) -> void:
	if _original_titles.has(base_name):
		tb.set_tab_title(i, _original_titles[base_name])
		_original_titles.erase(base_name)
	elif current.begins_with(TITLE_PREFIX):
		tb.set_tab_title(i, current.substr(TITLE_PREFIX.length()))
	tb.set_tab_icon(i, null)


## Locate (and cache) the editor's scene TabBar. Anchored on the internal
## `EditorSceneTabs` node (there is exactly one), then its descendant TabBar.
func _get_tabbar() -> TabBar:
	if _tabbar != null and is_instance_valid(_tabbar):
		return _tabbar
	if _editor_interface == null:
		return null
	var base := _editor_interface.get_base_control()
	if base == null:
		return null
	# Walk from the topmost node so the search covers the whole editor window.
	var top: Node = base
	while top.get_parent() != null:
		top = top.get_parent()
	var tabs_host := _find_by_class_contains(top, "EditorSceneTabs")
	if tabs_host != null:
		_tabbar = _first_tabbar_descendant(tabs_host)
	if _tabbar == null and not _warned_missing:
		_warned_missing = true
		push_warning("[IDTXFlow] Could not locate the editor scene TabBar; tab session markers disabled (viewport border/badge still active).")
	return _tabbar


func _find_by_class_contains(node: Node, class_substr: String) -> Node:
	if node.get_class().contains(class_substr):
		return node
	for c in node.get_children():
		var found := _find_by_class_contains(c, class_substr)
		if found != null:
			return found
	return null


func _first_tabbar_descendant(node: Node) -> TabBar:
	if node is TabBar:
		return node as TabBar
	for c in node.get_children():
		var found := _first_tabbar_descendant(c)
		if found != null:
			return found
	return null


## Normalize a tab title to a scene basename (no extension): strip our own prefix
## and the editor's trailing "(*)" unsaved marker.
static func _tab_title_to_basename(title: String) -> String:
	var t := title
	if t.begins_with(TITLE_PREFIX):
		t = t.substr(TITLE_PREFIX.length())
	t = t.trim_suffix("(*)").strip_edges()
	return t


## A green filled circle used as the tab status icon.
func _make_dot_icon() -> Texture2D:
	var size := 16
	var img := Image.create(size, size, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	var c := Vector2(size / 2.0, size / 2.0)
	var r := size / 2.0 - 1.0
	var green := Color(0.24, 0.82, 0.36)
	for y in size:
		for x in size:
			if Vector2(x + 0.5, y + 0.5).distance_to(c) <= r:
				img.set_pixel(x, y, green)
	return ImageTexture.create_from_image(img)
