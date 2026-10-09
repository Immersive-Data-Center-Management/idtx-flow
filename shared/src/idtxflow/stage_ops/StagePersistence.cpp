/**
 * @file StagePersistence.cpp
 * @brief Flatten a live stage's layer stack (session edits over the source root
 *        stack) and export it to a new file. See StagePersistence.h for the
 *        behavior contract.
 */
#include <idtxflow/stage_ops/StagePersistence.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/zipFile.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>

namespace fs = std::filesystem;

namespace idtxflow
{
namespace stage_ops
{

namespace
{

/// RAII guard that recursively removes a directory on destruction.
struct TempDirGuard
{
    fs::path path;
    explicit TempDirGuard(fs::path p)
        : path(std::move(p))
    {
    }
    ~TempDirGuard()
    {
        if (path.empty()) return;
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string ToLower(const std::string& s)
{
    std::string lower;
    lower.reserve(s.size());
    for (char c: s)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower;
}

bool HasExtension(const std::string& path, const char* ext)
{
    return ToLower(path).ends_with(ext);
}

} // namespace

bool StagePersistence::IsValidTarget(const std::string& out_os_path, const std::string& cache_dir_os,
                                     std::string& out_error)
{
    if (out_os_path.empty())
    {
        out_error = "empty target path";
        return false;
    }
    // Never write into the throwaway USD download / .scn cache. Compare on a
    // normalized, lowercased prefix so a path inside the cache dir is caught
    // regardless of separator style or case (Windows paths are case-insensitive).
    if (!cache_dir_os.empty())
    {
        std::string target = ToLower(out_os_path);
        std::string cache = ToLower(cache_dir_os);
        for (char& c: target)
            if (c == '\\') c = '/';
        for (char& c: cache)
            if (c == '\\') c = '/';
        if (!cache.empty() && cache.back() == '/') cache.pop_back();
        if (target == cache || target.rfind(cache + "/", 0) == 0)
        {
            out_error = "target '" + out_os_path + "' is inside the USD cache directory";
            return false;
        }
    }
    return true;
}

bool StagePersistence::IsLocalWritableSource(const std::string& source_uri, std::string& out_error)
{
    if (source_uri.empty())
    {
        out_error = "empty source uri";
        return false;
    }
    const std::string lower = ToLower(source_uri);
    if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0)
    {
        out_error = "source '" + source_uri +
                    "' is remote (http/https); its on-disk "
                    "root is a disposable download cache, not a writable source";
        return false;
    }
    return true;
}

SaveResult StagePersistence::ExportFlattened(const pxr::UsdStageRefPtr& stage, const std::string& out_os_path)
{
    if (!stage) return {false, "null stage"};
    if (out_os_path.empty()) return {false, "empty target path"};
    if (HasExtension(out_os_path, ".usdz")) return ExportUsdz(stage, out_os_path);
    return ExportFlat(stage, out_os_path);
}

SaveResult StagePersistence::ExportFlat(const pxr::UsdStageRefPtr& stage, const std::string& out_os_path)
{
    // Merge the composed layer stack (session layer over the source root layer
    // stack) into a single layer, preserving all composition arcs.
    pxr::SdfLayerRefPtr merged = pxr::UsdUtilsFlattenLayerStack(stage);
    if (!merged) return {false, "UsdUtilsFlattenLayerStack returned null"};

    // Write to a temp sibling first, then atomically rename over the target so a
    // failed/partial export can never leave a truncated file. The target
    // extension (not the source's) selects the on-disk encoding (.usda/.usdc).
    const fs::path dst(out_os_path);
    std::error_code ec;
    if (!dst.parent_path().empty()) fs::create_directories(dst.parent_path(), ec);

    const fs::path tmp = dst.parent_path() / ("." + dst.stem().string() + "_idtx_save" + dst.extension().string());

    fs::remove(tmp, ec); // best-effort pre-clean

    if (!merged->Export(tmp.string()))
    {
        std::error_code rmec;
        fs::remove(tmp, rmec);
        return {false, "SdfLayer::Export failed for '" + tmp.string() + "'"};
    }

    fs::rename(tmp, dst, ec);
    if (ec)
    {
        std::error_code rmec;
        fs::remove(tmp, rmec);
        return {false, "atomic rename '" + tmp.string() + "' -> '" + dst.string() + "' failed: " + ec.message()};
    }

    IDTX_LOG(IDTX_INFO, "Saved flattened stage to '{}'.", out_os_path);
    return {true, ""};
}

SaveResult StagePersistence::ExportUsdz(const pxr::UsdStageRefPtr& stage, const std::string& out_os_path)
{
    pxr::SdfLayerRefPtr merged = pxr::UsdUtilsFlattenLayerStack(stage);
    if (!merged) return {false, "UsdUtilsFlattenLayerStack returned null"};

    const fs::path dst(out_os_path);
    std::error_code ec;
    if (!dst.parent_path().empty()) fs::create_directories(dst.parent_path(), ec);

    // A .usdz is a zip archive that cannot be patched in place. When the target
    // already exists we unpack it, replace its root layer with the merged layer,
    // and re-zip in the original entry order (root first, per the usdz spec).
    // When it does not exist yet, write a fresh single-layer package.
    const fs::path tmpDir = dst.parent_path() / ("." + dst.stem().string() + "_idtx_save_unpack");
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);
    if (ec) return {false, "could not create temp dir '" + tmpDir.string() + "': " + ec.message()};
    TempDirGuard dirGuard(tmpDir);

    std::vector<std::string> fileOrder;

    if (fs::exists(dst))
    {
        pxr::SdfZipFile zip = pxr::SdfZipFile::Open(out_os_path);
        if (!zip) return {false, "could not open usdz '" + out_os_path + "'"};
        for (auto it = zip.begin(); it != zip.end(); ++it)
        {
            const std::string name = *it;
            const pxr::SdfZipFile::FileInfo info = it.GetFileInfo();
            if (info.compressionMethod != 0)
                return {false, "compressed entry '" + name + "' in usdz; cannot re-package"};
            const char* dataPtr = it.GetFile();
            const std::size_t dataSize = info.size;
            if (!dataPtr && dataSize > 0) return {false, "could not read entry '" + name + "' from usdz"};
            const fs::path outFile = tmpDir / name;
            fs::create_directories(outFile.parent_path(), ec);
            std::ofstream ofs(outFile, std::ios::binary | std::ios::trunc);
            if (!ofs) return {false, "could not open '" + outFile.string() + "' for writing"};
            if (dataSize > 0) ofs.write(dataPtr, static_cast<std::streamsize>(dataSize));
            fileOrder.push_back(name);
        }
    }

    // Fresh package (or an empty source): the merged layer becomes the sole root.
    if (fileOrder.empty()) fileOrder.push_back(dst.stem().string() + ".usdc");

    // Replace the root layer entry (first) with the merged layer. Export via
    // SdfLayer::Export (NOT UsdStage::Export) so package-internal relative asset
    // paths are preserved; keep the original root-entry filename so its encoding
    // is kept.
    const std::string& rootName = fileOrder.front();
    const fs::path rootPath = tmpDir / rootName;
    fs::create_directories(rootPath.parent_path(), ec);
    fs::remove(rootPath, ec); // ensure writable
    if (!merged->Export(rootPath.string()))
        return {false, "failed to export merged root layer to '" + rootPath.string() + "'"};

    // Re-zip every entry (root layer first) into a temp package, then atomically
    // rename over the target.
    const fs::path tmpPkg = dst.parent_path() / ("." + dst.stem().string() + "_idtx_save.usdz");
    fs::remove(tmpPkg, ec);
    {
        pxr::SdfZipFileWriter writer = pxr::SdfZipFileWriter::CreateNew(tmpPkg.string());
        if (!writer) return {false, "could not create usdz '" + tmpPkg.string() + "'"};
        for (const std::string& name: fileOrder)
        {
            const fs::path absPath = tmpDir / name;
            if (writer.AddFile(absPath.string(), name).empty())
            {
                writer.Discard();
                std::error_code rmec;
                fs::remove(tmpPkg, rmec);
                return {false, "failed to add '" + name + "' to usdz"};
            }
        }
        if (!writer.Save())
        {
            std::error_code rmec;
            fs::remove(tmpPkg, rmec);
            return {false, "failed to save usdz '" + tmpPkg.string() + "'"};
        }
    }

    fs::rename(tmpPkg, dst, ec);
    if (ec)
    {
        std::error_code rmec;
        fs::remove(tmpPkg, rmec);
        return {false, "atomic rename '" + tmpPkg.string() + "' -> '" + dst.string() + "' failed: " + ec.message()};
    }

    IDTX_LOG(IDTX_INFO, "Saved flattened stage to usdz '{}'.", out_os_path);
    return {true, ""};
}

} // namespace stage_ops
} // namespace idtxflow
