/**
 * The only purpose of this file is to provide the required HttpAssetResolver registration/glue code that could not be
 * part of the header files in the shared code.
 *
 * NOTE: AR_DEFINE_RESOLVER is intentionally NOT used here. Registering UsdHttpAssetResolver as a direct ArResolver
 * subtype would make it a primary resolver candidate, conflicting with UsdGodotAssetResolver (which is the actual
 * primary resolver). UsdHttpAssetResolver is activated as a URI-scheme resolver via plugInfo.json
 * (uriSchemes: ["http", "https"]) when PXR_PLUGINPATH_NAME is set to include the plugin directory.
 **/
#include <pxr/usd/ar/defineResolver.h>
#include <pxr/usd/ar/resolver.h>

#include <idtxflow/resolver/HttpResolver.h>

using namespace pxr;

// Intentionally omitted: AR_DEFINE_RESOLVER(UsdHttpAssetResolver, ArResolver);
// See comment at top of file.
