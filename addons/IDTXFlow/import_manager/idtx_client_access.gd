@tool
extends RefCounted

## Stateless lookup for the native IdtxClient engine singleton.
##
## The client is created and owned natively and reached by its engine-singleton
## name; this helper centralizes that lookup so UI scripts share one call site
## instead of each resolving the singleton themselves. It creates nothing, holds
## no state, and never enters the scene tree.


static func get_client() -> Object:
	if not Engine.has_singleton("IdtxClient"):
		return null
	return Engine.get_singleton("IdtxClient")
