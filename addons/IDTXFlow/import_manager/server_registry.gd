@tool
extends RefCounted

## Registry of known asset servers and the usernames saved for each
##
## Storage (ProjectSettings, registered in plugin.gd):
##   idtxflow/import/servers     : Dictionary { url: { "users": [String], "last_user": String } }
##   idtxflow/import/last_server : String
##
## Only non-secret values (server URLs + usernames) are stored. Passwords and tokens are never persisted here

const SERVERS_KEY := "idtxflow/import/servers"
const LAST_SERVER_KEY := "idtxflow/import/last_server"


## The full servers map
static func servers() -> Dictionary:
	var m: Dictionary = ProjectSettings.get_setting(SERVERS_KEY, {})
	return m.duplicate(true)


## Known server URLs, sorted for a stable dropdown order
static func server_urls() -> Array:
	var urls: Array = servers().keys()
	urls.sort()
	return urls


## The last server URL used
static func last_server() -> String:
	return String(ProjectSettings.get_setting(LAST_SERVER_KEY, ""))


## Usernames remembered for a server
static func users_for(url: String) -> Array:
	var entry: Dictionary = servers().get(url, {})
	var users: Array = entry.get("users", [])
	return users.duplicate()


## The last username used for a server
static func last_user_for(url: String) -> String:
	var entry: Dictionary = servers().get(url, {})
	return String(entry.get("last_user", ""))


## Add or update a server entry and persist it. Appends the username to the
## server's user list if new, marks it as the server's last_user and saves ProjectSettings
static func add_entry(url: String, username: String) -> void:
	url = url.strip_edges()
	username = username.strip_edges()
	if url.is_empty():
		return

	var map: Dictionary = servers()
	var entry: Dictionary = map.get(url, {"users": [], "last_user": ""})
	var users: Array = entry.get("users", [])
	if not username.is_empty() and not users.has(username):
		users.append(username)
	entry["users"] = users
	if not username.is_empty():
		entry["last_user"] = username
	map[url] = entry

	ProjectSettings.set_setting(SERVERS_KEY, map)
	ProjectSettings.set_setting(LAST_SERVER_KEY, url)
	ProjectSettings.save()


## Hide the radio bullets an OptionButton draws left of each item
static func hide_option_button_bullets(option_button: OptionButton) -> void:
	if option_button == null:
		return
	var popup := option_button.get_popup()
	if popup == null:
		return
	var blank := ImageTexture.create_from_image(Image.create(1, 1, false, Image.FORMAT_RGBA8))
	popup.add_theme_icon_override("radio_checked", blank)
	popup.add_theme_icon_override("radio_unchecked", blank)
	popup.add_theme_icon_override("checked", blank)
	popup.add_theme_icon_override("unchecked", blank)
