@tool
extends RefCounted

## Holds the active asset-server connection state.
##
## This is the shared handoff between the login step and the server-browse
## step. When we later replace the mock backend with real HTTP calls, this
## is the object we keep passing around.

var server_url: String = ""
var authenticated: bool = false
var auth_token: String = ""  # placeholder for future real auth
var username: String = ""
var remember_credentials: bool = false


func reset() -> void:
	server_url = ""
	authenticated = false
	auth_token = ""
	username = ""
	remember_credentials = false