#include "UsdGodotAssetResolver.h"

#include <string>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/ar/filesystemAsset.h>
#include <pxr/usd/ar/filesystemWritableAsset.h>

#include "idtxflow/resolver/HttpResolver.h"

PXR_NAMESPACE_OPEN_SCOPE
    TF_DEFINE_PUBLIC_TOKENS(GodotResolverTokens, 
        ((resScheme, "res"))
        ((userScheme, "user"))
    );
PXR_NAMESPACE_CLOSE_SCOPE

using namespace godot;
using namespace pxr;

AR_DEFINE_RESOLVER(UsdGodotAssetResolver, ArResolver);

#ifdef __ANDROID__
class GodotBufferedAsset : public ArAsset {
    std::shared_ptr<const char> _buffer;
    size_t _size;
public:
    GodotBufferedAsset(std::shared_ptr<const char> buffer, size_t size)
        : _buffer(std::move(buffer)), _size(size) {}
    
    size_t GetSize() const override { return _size; }
    
    std::shared_ptr<const char> GetBuffer() const override { return _buffer; }
    
    size_t Read(void* buffer, size_t count, size_t offset) const override {
        size_t bytesToRead = std::min(count, _size - std::min(offset, _size));
        memcpy(buffer, _buffer.get() + offset, bytesToRead);
        return bytesToRead;
    }
    
    std::pair<FILE*, size_t> GetFileUnsafe() const override {
        return {nullptr, 0}; // Not backed by a FILE*
    }
};
#endif

std::string UsdGodotAssetResolver::_GetExtension(const std::string& path) const
{
    return TfGetExtension(path);
}

std::string UsdGodotAssetResolver::_CreateIdentifier(const std::string& assetPath,
    const ArResolvedPath& anchorAssetPath) const
{
    // if the asset path already starts with our uri-scheme it is treated as absolute
    // and thus anchored and can be used as identifier
    if (TfStringStartsWith(assetPath, GodotResolverTokens->resScheme.GetString() + "://") ||
        TfStringStartsWith(assetPath, GodotResolverTokens->userScheme.GetString() + "://")) {
        return assetPath;  // Already absolute, return as-is
    }
    
    // if the given path is relative and we do have an anchor path given, the relative path will be anchored
    // at the anchor path
    if (!anchorAssetPath.IsEmpty())
    {
        std::string anchorDir = TfGetPathName(anchorAssetPath.GetPathString());
        
        // Strip URI scheme from anchorDir so TfStringCatPaths/TfNormPath work correctly
        std::string scheme;
        const std::string resPfx = GodotResolverTokens->resScheme.GetString() + "://";
        const std::string userPfx = GodotResolverTokens->userScheme.GetString() + "://";
        
        if (TfStringStartsWith(anchorDir, resPfx)) {
            scheme = resPfx;
            anchorDir = anchorDir.substr(resPfx.size());
        } else if (TfStringStartsWith(anchorDir, userPfx)) {
            scheme = userPfx;
            anchorDir = anchorDir.substr(userPfx.size());
        }
        
        std::string resolvedPath = TfStringCatPaths(anchorDir, assetPath);
        return scheme + TfNormPath(resolvedPath);
    }
    
    // coming here we seem to have an relative path w/o an anchor, thus we will make it simply "absolute"
    // by adding the "res://" schema to it
    return GodotResolverTokens->resScheme.GetString() +  "://" + assetPath;
}

std::string UsdGodotAssetResolver::_CreateIdentifierForNewAsset(const std::string& assetPath,
    const ArResolvedPath& anchorAssetPath) const
{
    // lets do the same as in _CreateIdentifier for the time being
    return _CreateIdentifier(assetPath, anchorAssetPath);
}

ArResolvedPath UsdGodotAssetResolver::_Resolve(const std::string& assetPath) const
{
    // If this path doesn't start with our scheme, we don't handle it. But, why is it passed to this resolver?
    if (!TfStringStartsWith(assetPath, GodotResolverTokens->resScheme.GetString() + "://") &&
        !TfStringStartsWith(assetPath, GodotResolverTokens->userScheme.GetString() + "://")) {
        // return as unresolved/empty path, if this is not ours
        return ArResolvedPath();
    }
    
    // returning the path as is (we may have resolved placeholder variables or what ever into a complete path)
    // will keep this resolver to open this file.
    // If we return a local path here (resolving the res:// to an absolute path) the default resolver will open this asset
    return ArResolvedPath(assetPath);
}

ArResolvedPath UsdGodotAssetResolver::_ResolveForNewAsset(const std::string& assetPath) const
{
    // for the time being the resolving of the path for new assets is the same as for existing ones
    return Resolve(assetPath);
}

std::shared_ptr<ArAsset> UsdGodotAssetResolver::_OpenAsset(const ArResolvedPath& resolvedPath) const
{
    // we retrieve the resolved path of the asset to open the same
    // this is essentially a "res://path/to/file.usd". So calculate a real local path openUSD is capable of finding
    // and open the file from there
    const std::string assetPath = resolvedPath.GetPathString();
        
    print_error("TEST:" + String("open stage at: ") + assetPath.c_str());

    // path resolution for user:// and res:// need to be treated differently, especialy on android
    // as assets accessed with "res://" are usually packed inside the APK binary and could not be accessed with openUSD
    // default file access
#ifdef __ANDROID__
    if (TfStringStartsWith(assetPath, GodotResolverTokens->userScheme.GetString() + "://"))
    {
#endif
        if (ProjectSettings *project_settings = ProjectSettings::get_singleton())
        {
            // Convert the user:// path to an absolute path
            String absolute_path = project_settings->globalize_path(resolvedPath.GetPathString().c_str());
            return ArFilesystemAsset::Open(ArResolvedPath(absolute_path.utf8().get_data()));
        }
        
        return nullptr;
#ifdef __ANDROID__        
    }
    // special handling for android if the file is located in the package (res:// case)
    // Godot's FileAccess abstraction is able to read the file contents even when inside a APK/PCK
    Ref<FileAccess> file = FileAccess::open(String(assetPath.c_str()), FileAccess::READ);
    if (!file.is_valid())
    {
        print_error("TEST: Unable to open file: " + String(assetPath.c_str()));
        return nullptr;
    }
    
    size_t size = file->get_length();
    std::shared_ptr<char> buffer(new char[size], std::default_delete<char[]>());
    PackedByteArray data = file->get_buffer(size);
    memcpy(buffer.get(), data.ptr(), size);
    print_error("TEST: Stage opened and contents passed to BufferedAsset");
    return std::make_shared<GodotBufferedAsset>(std::move(buffer), size);
#endif
    return nullptr;
}

std::shared_ptr<ArWritableAsset> UsdGodotAssetResolver::_OpenAssetForWrite(
    const ArResolvedPath& resolvedPath,
    WriteMode writeMode) const
{
    const std::string assetPath = resolvedPath.GetPathString();
    if (TfStringStartsWith(assetPath, GodotResolverTokens->resScheme.GetString() + "://"))
    {
        // res:// asset path is usually packed with the application and should not be writeable at all
        return nullptr;
    }
    
    // we retrieve the resolved path of the asset to open the same
    // this is essentially a "res://path/to/file.usd". So calculate a real local path openUSD is capable of finding
    // and open the file for write there
    if (ProjectSettings *project_settings = ProjectSettings::get_singleton())
    {
        // Convert the res:// path to an absolute path
        String absolute_path = project_settings->globalize_path(resolvedPath.GetPathString().c_str());

        // Return the result wrapped in an ArResolvedPath.
        // This indicates to USD that the path is now "resolved" and can be opened.
        return ArFilesystemWritableAsset::Create(
            ArResolvedPath(absolute_path.utf8().get_data()),
            writeMode);
    }
    
    return nullptr;
}