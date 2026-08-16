@tool
extends RefCounted

## Base interface for a data source that feeds `WizardFileBrowser`.
##
## The browser is source-agnostic: it never touches `DirAccess`/`FileAccess`
## nor the IDTX backend directly. Instead it asks a provider to list a
## directory and renders whatever entries come back. Listing is async-friendly
## (results arrive via `entries_ready` / `list_failed`) so remote providers can
## resolve over HTTP without blocking.
##
## Concrete providers:
##   • LocalFileProvider  - res:// / user:// / filesystem via DirAccess.
##   • ServerFileProvider - IDTX asset server via IdtxClient (flat, grouped).
##
## Entry dict shape (one per row the browser should show):
##   {
##     "name":       String,      # display label
##     "is_dir":     bool,        # directory header / navigable folder
##     "path":       String,      # full path used for selection contract
##     "selectable": bool,        # false for non-selectable dir headers
##     "meta":       Dictionary,  # extra metadata (empty for local files)
##   }

## Emitted when a `list_dir()` request resolves. `dir` echoes the requested
## directory so late/stale responses can be ignored by the browser.
signal entries_ready(dir: String, entries: Array)

## Emitted when a `list_dir()` request fails (e.g. HTTP error, backend down).
signal list_failed(dir: String, message: String)


## Request a listing of `dir`. Implementations resolve asynchronously and emit
## `entries_ready(dir, entries)` on success or `list_failed(dir, message)` on
## failure. Synchronous providers (local) may emit immediately.
func list_dir(_dir: String) -> void:
	push_error("[IDTXFlow] FileProvider.list_dir() not implemented.")


## Whether `dir` exists / can be navigated to. Used by the browser to validate
## typed paths. Remote providers that don't support navigation may just return
## whether `dir` equals the root prefix.
func dir_exists(_dir: String) -> bool:
	return false


## The root prefix for this provider (e.g. "res://", "user://", "" for the
## OS filesystem, or a server URL for the asset server). Used as the starting
## directory and the boundary for "go to parent".
func get_root_prefix() -> String:
	return ""


## When false, the browser hides/disables its navigation chrome (back/forward/
## up + editable path) and skips history — suitable for a flat, grouped listing
## like today's server view. Flip to true once the provider can resolve
## subdirectories for a navigable tree.
func supports_navigation() -> bool:
	return true