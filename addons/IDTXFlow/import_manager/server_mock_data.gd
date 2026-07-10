@tool
extends RefCounted

## MOCK asset-server data. Provides a fake directory tree with fake file
## metadata so the server-browse UI is fully navigable without any real
## backend connectivity.
##
## When a real asset-server integration is wired up, replace the two
## static methods (`get_directory` and `get_credentials_valid`) with actual
## HTTP calls. Nothing else in the wizard needs to change.

const ROOT_PATH := "/"

# Demo credentials that "succeed" (any other pair fails).
const DEMO_USERNAME := "demo"
const DEMO_PASSWORD := "demo"


static func get_credentials_valid(username: String, password: String) -> bool:
	return username == DEMO_USERNAME and password == DEMO_PASSWORD


# Returns entries for the given server-side path. Each entry is a Dictionary:
#   {
#     "name":        String,   # display name
#     "is_dir":      bool,
#     "path":        String,   # absolute server path (starts with "/")
#     "size_bytes":  int,      # 0 for directories
#     "modified":    String,   # ISO-ish date, e.g. "2026-05-29"
#     "description": String,   # optional
#   }
static func get_directory(path: String) -> Array:
	var norm := _normalize(path)
	match norm:
		"/":
			return [
				_dir("scenes",   "/scenes",   "Prepared show scenes"),
				_dir("vehicles", "/vehicles", "Rigged and unrigged vehicles"),
				_dir("props",    "/props",    "Small hero props"),
				_file("welcome.usda",       "/welcome.usda",        2_412,     "2026-06-01"),
				_file("company_logo.usdz",  "/company_logo.usdz",   1_204_050, "2026-05-12"),
			]
		"/scenes":
			return [
				_dir("interiors", "/scenes/interiors", ""),
				_dir("exteriors", "/scenes/exteriors", ""),
				_file("hero_shot.usda",   "/scenes/hero_shot.usda",   5_982_141,  "2026-05-29"),
				_file("beauty_shot.usdz", "/scenes/beauty_shot.usdz", 12_483_112, "2026-05-27"),
			]
		"/scenes/interiors":
			return [
				_file("living_room.usda",  "/scenes/interiors/living_room.usda",  3_204_811, "2026-04-14"),
				_file("kitchen.usda",      "/scenes/interiors/kitchen.usda",     2_812_003, "2026-04-14"),
				_file("workshop.usdc",     "/scenes/interiors/workshop.usdc",    4_620_710, "2026-05-02"),
			]
		"/scenes/exteriors":
			return [
				_file("city_block.usdz",   "/scenes/exteriors/city_block.usdz",   18_402_112, "2026-03-22"),
				_file("garden.usda",       "/scenes/exteriors/garden.usda",       1_004_012,  "2026-03-22"),
			]
		"/vehicles":
			return [
				_file("sedan.usdz",     "/vehicles/sedan.usdz",     8_412_501, "2026-05-01"),
				_file("suv.usdz",       "/vehicles/suv.usdz",       9_240_010, "2026-05-01"),
				_file("bicycle.usda",   "/vehicles/bicycle.usda",     414_820, "2026-04-30"),
				_file("truck.usdc",     "/vehicles/truck.usdc",    11_002_400, "2026-05-05"),
			]
		"/props":
			return [
				_file("chair.usda",     "/props/chair.usda",       182_004, "2026-02-10"),
				_file("lamp.usdz",      "/props/lamp.usdz",        612_400, "2026-02-11"),
				_file("bookshelf.usda", "/props/bookshelf.usda",   904_120, "2026-02-14"),
			]
		_:
			return []


static func get_entry(path: String) -> Dictionary:
	# Look up a single entry by path — walk the parent directory and match.
	var norm := _normalize(path)
	if norm == "/" or norm.is_empty():
		return {}
	var last_slash := norm.rfind("/")
	var parent := "/" if last_slash <= 0 else norm.substr(0, last_slash)
	var entries := get_directory(parent)
	for e in entries:
		if e.get("path", "") == norm:
			return e
	return {}


static func _normalize(path: String) -> String:
	if path.is_empty():
		return ROOT_PATH
	var p := path
	if not p.begins_with("/"):
		p = "/" + p
	if p.length() > 1 and p.ends_with("/"):
		p = p.substr(0, p.length() - 1)
	return p


static func _dir(name: String, path: String, description: String = "") -> Dictionary:
	return {
		"name": name,
		"is_dir": true,
		"path": path,
		"size_bytes": 0,
		"modified": "",
		"description": description,
	}


static func _file(name: String, path: String, size_bytes: int, modified: String, description: String = "") -> Dictionary:
	return {
		"name": name,
		"is_dir": false,
		"path": path,
		"size_bytes": size_bytes,
		"modified": modified,
		"description": description,
	}