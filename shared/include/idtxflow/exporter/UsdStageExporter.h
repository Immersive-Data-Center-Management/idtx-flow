#pragma once

/**
 * @file UsdStageExporter.h
 * @brief Engine-agnostic orchestrator that authors a USD stage from neutral export descriptions.
 *
 * This is the export counterpart of UsdStageConverter (StageConverter.h). It walks a tree of
 * NodeExportDescription structs and authors the matching USD prims (Xform / Mesh / ...), then
 * writes the stage to disk. It only ever reads the neutral structs and pxr types — never godot::.
 *
 * NOTE (scaffold state): geometry, materials and skeletons are not authored yet. The current
 * implementation establishes the correct call path end-to-end and produces a valid USD file with
 * the node hierarchy and transforms. Mesh/material/skeleton authoring is filled in per phase — see
 * docs/EXPORTER_DESIGN.md §10.
 */

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <idtxflow/utils/Logger.h>

#include "../types/TargetTypes.h"
#include "ExportDescription.h"
#include "FromTypeConverter.h"

namespace idtxflow
{
namespace exporter
{
    /**
     * Sanitize an arbitrary node name into a valid USD prim identifier: only [A-Za-z0-9_], and it
     * may not start with a digit. Invalid characters become underscores. Empty names become "Node".
     */
    inline std::string SanitizePrimName(const std::string& name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            out.push_back((std::isalnum(uc) || c == '_') ? c : '_');
        }
        if (out.empty()) out = "Node";
        if (std::isdigit(static_cast<unsigned char>(out.front()))) out.insert(out.begin(), '_');
        return out;
    }

    template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
    class UsdStageExporter
    {
        IDTX_LOG_CATEGORY("StageExporter")

    public:
        using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
        using FromConverter = FromTypeConverter<TargetEngine>;
        using NodeDesc = NodeExportDescription<TargetEngine>;

        /**
         * Author a complete USD stage from `root` and write it to `options.outputStagePath`.
         * The output format (ascii/binary/package) is derived from the file extension by USD.
         * @return true on success, false if the stage could not be created or saved.
         */
        bool Export(const NodeDesc& root, const StageExportOptions& options)
        {
            // Author into an in-memory stage, then Export() to the target path. Export overwrites an
            // existing file and picks the format (ascii/binary/package) from the path's extension —
            // unlike CreateNew, which fails if the file already exists.
            pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory();
            if (!stage)
            {
                IDTX_LOGF(IDTX_ERROR, "Unable to create in-memory USD stage for '{}'", options.outputStagePath);
                return false;
            }

            // Author as Y-up / metersPerUnit=1 by default so Godot transforms need no per-node fixup.
            pxr::UsdGeomSetStageUpAxis(stage, options.upAxisY ? pxr::UsdGeomTokens->y : pxr::UsdGeomTokens->z);
            pxr::UsdGeomSetStageMetersPerUnit(stage, 1.0);

            pxr::UsdPrim rootPrim = ExportNode(stage, pxr::SdfPath::AbsoluteRootPath(), root, options);
            if (rootPrim)
            {
                stage->SetDefaultPrim(rootPrim);
            }

            if (!stage->GetRootLayer()->Export(options.outputStagePath))
            {
                // GetRootLayer()->Export fails for .usdz (anonymous in-memory layer can't be
                // packaged directly). Fall back to UsdStage::Export which goes through the stage
                // flattener and can create a proper USDZ archive.
                if (!stage->Export(options.outputStagePath))
                {
                    IDTX_LOGF(IDTX_ERROR, "Unable to write USD stage to '{}'", options.outputStagePath);
                    return false;
                }
            }

            IDTX_LOGF(IDTX_INFO, "Exported USD stage to '{}'", options.outputStagePath);
            return true;
        }

    protected:
        /**
         * Author the prim for a single node, then recurse into its children. Returns the created
         * prim so the caller can use it (e.g. as the stage's default prim).
         */
        pxr::UsdPrim ExportNode(
            const pxr::UsdStageRefPtr& stage,
            const pxr::SdfPath& parentPath,
            const NodeDesc& desc,
            const StageExportOptions& options)
        {
            const pxr::SdfPath path = parentPath.AppendChild(pxr::TfToken(SanitizePrimName(desc.primName)));

            pxr::UsdPrim prim;
            switch (desc.kind)
            {
                case NodeDesc::Kind::Mesh:     prim = ExportMesh(stage, path, desc, options); break;
                case NodeDesc::Kind::Skeleton: prim = ExportXform(stage, path, desc); break;  // TODO(phase 4): UsdSkel
                case NodeDesc::Kind::Xform:
                default:                       prim = ExportXform(stage, path, desc); break;
            }

            for (const NodeDesc& child : desc.children)
            {
                ExportNode(stage, path, child, options);
            }

            return prim;
        }

        /** Author a UsdGeomXform carrying the node's local transform as a single matrix xformOp. */
        pxr::UsdPrim ExportXform(
            const pxr::UsdStageRefPtr& stage, const pxr::SdfPath& path, const NodeDesc& desc)
        {
            pxr::UsdGeomXform xform = pxr::UsdGeomXform::Define(stage, path);
            xform.AddTransformOp().Set(desc.localTransform);
            return xform.GetPrim();
        }

        /**
         * Author a UsdGeomMesh prim. Geometry (points / faces / normals / UVs / vertex colors) is
         * accumulated from all surfaces in `desc` into a single mesh; surfaces with more than one
         * entry become UsdGeomSubsets (one per surface) for per-surface material binding.
         *
         * UV V-flip: the importer applied `uv.y = -uv.y` (FlipUvV). The exporter undoes this by
         * writing `st.y = -uv.y`, restoring the original USD coordinates.
         *
         * Interpolation: Godot stores normals and UVs as per-vertex arrays (same size as Vertices,
         * indexed by the same Triangles index buffer). This maps to USD `vertex` interpolation.
         *
         * Materials: if `desc.surfaceMaterials[s]` has a value, a UsdPreviewSurface network is
         * authored under `meshPath/Looks/matName` and bound to the mesh or its GeomSubset.
         */
        pxr::UsdPrim ExportMesh(
            const pxr::UsdStageRefPtr& stage, const pxr::SdfPath& path,
            const NodeDesc& desc, const StageExportOptions& options)
        {
            pxr::UsdGeomMesh mesh = pxr::UsdGeomMesh::Define(stage, path);
            mesh.AddTransformOp().Set(desc.localTransform);

            // Godot (and the importer) use left-handed / CW face winding. USD defaults to
            // rightHanded (CCW). Without this attribute, usdView treats all faces as back-facing
            // from outside the mesh and shows them as black. Setting leftHanded makes the front
            // face match the winding order in the exported index arrays.
            mesh.CreateOrientationAttr(pxr::VtValue(pxr::UsdGeomTokens->leftHanded));

            if (desc.surfaces.empty())
                return mesh.GetPrim();

            // One pass to decide which optional channels to author (all-or-nothing per channel).
            bool hasNormals = false, hasUVs = false, hasColors = false;
            for (const auto& md : desc.surfaces)
            {
                if (!md.Normals.is_empty())      hasNormals = true;
                if (!md.UVs.is_empty())          hasUVs     = true;
                if (!md.VertexColors.is_empty()) hasColors  = true;
            }

            // Accumulation buffers (std::vector for push_back; converted to VtArray before authoring).
            std::vector<pxr::GfVec3f> points;
            std::vector<int>          fvi;      // faceVertexIndices
            std::vector<int>          fvc;      // faceVertexCounts (all 3)
            std::vector<pxr::GfVec3f> normals;
            std::vector<pxr::GfVec2f> st;       // UVs with V un-flipped
            std::vector<pxr::GfVec3f> colors;

            std::vector<std::pair<int,int>> faceRanges;  // {faceStart, faceCount} per surface
            int vertexOffset = 0;
            int faceOffset   = 0;

            for (const auto& md : desc.surfaces)
            {
                const int vcount = static_cast<int>(md.Vertices.size());
                const int icount = static_cast<int>(md.Triangles.size());
                const int faceCount = (icount > 0) ? (icount / 3) : (vcount / 3);

                // Points
                for (int i = 0; i < vcount; ++i)
                    points.push_back(pxr::GfVec3f(md.Vertices[i].x, md.Vertices[i].y, md.Vertices[i].z));

                // Face vertex indices: use Triangles if present, else generate sequential.
                if (icount > 0)
                {
                    for (int i = 0; i < icount; ++i)
                        fvi.push_back(md.Triangles[i] + vertexOffset);
                }
                else
                {
                    for (int i = 0; i < vcount; ++i)
                        fvi.push_back(vertexOffset + i);
                }

                // Face vertex counts (triangles = 3 each).
                for (int f = 0; f < faceCount; ++f)
                    fvc.push_back(3);

                // Normals (vertex interpolation — parallel to points array).
                if (hasNormals)
                {
                    for (int i = 0; i < vcount; ++i)
                    {
                        const pxr::GfVec3f n = (i < static_cast<int>(md.Normals.size()))
                            ? pxr::GfVec3f(md.Normals[i].x, md.Normals[i].y, md.Normals[i].z)
                            : pxr::GfVec3f(0.f, 1.f, 0.f);  // safe up-normal fallback
                        normals.push_back(n);
                    }
                }

                // UVs: invert the import V-flip (import: uv.y = -uv.y → export: st.y = -uv.y).
                if (hasUVs)
                {
                    for (int i = 0; i < vcount; ++i)
                    {
                        const pxr::GfVec2f s = (i < static_cast<int>(md.UVs.size()))
                            ? pxr::GfVec2f(md.UVs[i].x, -md.UVs[i].y)
                            : pxr::GfVec2f(0.f, 0.f);
                        st.push_back(s);
                    }
                }

                // Display colors (RGB; alpha deferred to Phase 3 opacity).
                if (hasColors)
                {
                    for (int i = 0; i < vcount; ++i)
                    {
                        const pxr::GfVec3f c = (i < static_cast<int>(md.VertexColors.size()))
                            ? pxr::GfVec3f(md.VertexColors[i].r, md.VertexColors[i].g, md.VertexColors[i].b)
                            : pxr::GfVec3f(1.f, 1.f, 1.f);
                        colors.push_back(c);
                    }
                }

                faceRanges.push_back({faceOffset, faceCount});
                vertexOffset += vcount;
                faceOffset   += faceCount;
            }

            // Author core attributes.
            mesh.CreatePointsAttr(pxr::VtValue(pxr::VtArray<pxr::GfVec3f>(points.begin(), points.end())));
            mesh.CreateFaceVertexIndicesAttr(pxr::VtValue(pxr::VtArray<int>(fvi.begin(), fvi.end())));
            mesh.CreateFaceVertexCountsAttr(pxr::VtValue(pxr::VtArray<int>(fvc.begin(), fvc.end())));

            if (hasNormals)
            {
                mesh.CreateNormalsAttr(pxr::VtValue(pxr::VtArray<pxr::GfVec3f>(normals.begin(), normals.end())));
                mesh.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
            }

            pxr::UsdGeomPrimvarsAPI pvAPI(mesh);

            if (hasUVs)
            {
                pxr::UsdGeomPrimvar stPV = pvAPI.CreatePrimvar(
                    pxr::TfToken("st"),
                    pxr::SdfValueTypeNames->TexCoord2fArray,
                    pxr::UsdGeomTokens->vertex);
                stPV.Set(pxr::VtArray<pxr::GfVec2f>(st.begin(), st.end()));
            }

            if (hasColors)
            {
                pxr::UsdGeomPrimvar colorPV = pvAPI.CreatePrimvar(
                    pxr::TfToken("displayColor"),
                    pxr::SdfValueTypeNames->Color3fArray,
                    pxr::UsdGeomTokens->vertex);
                colorPV.Set(pxr::VtArray<pxr::GfVec3f>(colors.begin(), colors.end()));
            }

            // GeomSubsets — one per surface when there are multiple surfaces.
            const bool multiSurface = (desc.surfaces.size() > 1);
            if (multiSurface)
            {
                for (size_t s = 0; s < faceRanges.size(); ++s)
                {
                    const auto& [faceStart, faceCount] = faceRanges[s];
                    std::vector<int> idx;
                    idx.reserve(faceCount);
                    for (int f = 0; f < faceCount; ++f)
                        idx.push_back(faceStart + f);

                    pxr::UsdGeomSubset::CreateGeomSubset(
                        mesh,
                        pxr::TfToken("surface_" + std::to_string(s)),
                        pxr::UsdGeomTokens->face,
                        pxr::VtArray<int>(idx.begin(), idx.end()));
                }
            }

            // Material binding — author UsdPreviewSurface under meshPath/Looks/ and bind.
            const pxr::SdfPath looksPath = path.AppendChild(pxr::TfToken("Looks"));
            for (size_t s = 0; s < desc.surfaceMaterials.size(); ++s)
            {
                if (!desc.surfaceMaterials[s].has_value()) continue;

                pxr::UsdShadeMaterial mat;
                ExportMaterial(stage, looksPath, *desc.surfaceMaterials[s], options, mat);
                if (!mat) continue;

                if (multiSurface)
                {
                    const pxr::SdfPath subsetPath = path.AppendChild(
                        pxr::TfToken("surface_" + std::to_string(s)));
                    pxr::UsdPrim subsetPrim = stage->GetPrimAtPath(subsetPath);
                    if (subsetPrim)
                        pxr::UsdShadeMaterialBindingAPI::Apply(subsetPrim).Bind(mat);
                }
                else
                {
                    pxr::UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim()).Bind(mat);
                }
            }

            return mesh.GetPrim();
        }

        /**
         * Author a UsdShadeMaterial (UsdPreviewSurface + per-channel UsdUVTexture nodes) from a
         * neutral MaterialExportDescription and write any texture image files to textureOutputDir.
         * Sets `outMaterial` to the created material prim on success.
         */
        void ExportMaterial(
            const pxr::UsdStageRefPtr& stage,
            const pxr::SdfPath& looksPath,
            const MaterialExportDescription& matDesc,
            const StageExportOptions& options,
            pxr::UsdShadeMaterial& outMaterial)
        {
            using MC = types::MaterialChannel;

            // Ensure the texture output directory exists.
            if (!options.textureOutputDir.empty())
                std::filesystem::create_directories(options.textureOutputDir);

            const pxr::SdfPath matPath    = looksPath.AppendChild(pxr::TfToken(matDesc.name));
            const pxr::SdfPath shaderPath = matPath.AppendChild(pxr::TfToken("Preview"));
            const pxr::SdfPath stPath     = matPath.AppendChild(pxr::TfToken("st_reader"));

            pxr::UsdShadeMaterial mat    = pxr::UsdShadeMaterial::Define(stage, matPath);
            pxr::UsdShadeShader  shader  = pxr::UsdShadeShader::Define(stage, shaderPath);
            pxr::UsdShadeShader  stRdr   = pxr::UsdShadeShader::Define(stage, stPath);

            shader.CreateIdAttr(pxr::VtValue(pxr::TfToken("UsdPreviewSurface")));
            stRdr.CreateIdAttr(pxr::VtValue(pxr::TfToken("UsdPrimvarReader_float2")));
            stRdr.CreateInput(pxr::TfToken("varname"), pxr::SdfValueTypeNames->Token)
                .Set(pxr::TfToken("st"));
            pxr::UsdShadeOutput stOut = stRdr.CreateOutput(
                pxr::TfToken("result"), pxr::SdfValueTypeNames->Float2);

            // Helper: write PNG bytes to disk and author a UsdUVTexture; returns its named output.
            auto authorTex = [&](const TextureExportRef& ref,
                                  const std::string& texPrimName,
                                  const pxr::TfToken& outputName,
                                  const pxr::SdfValueTypeName& outputType) -> pxr::UsdShadeOutput
            {
                // Write file
                if (!ref.imageBytes.empty() && !options.textureOutputDir.empty())
                {
                    const std::string outFile =
                        options.textureOutputDir + "/" + ref.sourceImagePath;
                    std::ofstream ofs(outFile, std::ios::binary);
                    if (ofs)
                        ofs.write(reinterpret_cast<const char*>(ref.imageBytes.data()),
                                  static_cast<std::streamsize>(ref.imageBytes.size()));
                }

                const pxr::SdfPath texPath = matPath.AppendChild(
                    pxr::TfToken(SanitizePrimName(texPrimName)));
                pxr::UsdShadeShader texShader = pxr::UsdShadeShader::Define(stage, texPath);
                texShader.CreateIdAttr(pxr::VtValue(pxr::TfToken("UsdUVTexture")));
                texShader.CreateInput(pxr::TfToken("st"), pxr::SdfValueTypeNames->Float2)
                    .ConnectToSource(stOut);
                texShader.CreateInput(pxr::TfToken("file"), pxr::SdfValueTypeNames->Asset)
                    .Set(pxr::SdfAssetPath("./" + ref.sourceImagePath));
                texShader.CreateInput(pxr::TfToken("wrapS"), pxr::SdfValueTypeNames->Token)
                    .Set(pxr::TfToken("repeat"));
                texShader.CreateInput(pxr::TfToken("wrapT"), pxr::SdfValueTypeNames->Token)
                    .Set(pxr::TfToken("repeat"));
                if (ref.uvScale != pxr::GfVec2f(1, 1))
                    texShader.CreateInput(pxr::TfToken("scale"), pxr::SdfValueTypeNames->Float4)
                        .Set(pxr::GfVec4f(ref.uvScale[0], ref.uvScale[1], 1.f, 1.f));
                if (ref.uvOffset != pxr::GfVec2f(0, 0))
                    texShader.CreateInput(pxr::TfToken("bias"), pxr::SdfValueTypeNames->Float4)
                        .Set(pxr::GfVec4f(ref.uvOffset[0], ref.uvOffset[1], 0.f, 0.f));
                return texShader.CreateOutput(outputName, outputType);
            };

            // diffuseColor
            {
                auto inp = shader.CreateInput(pxr::TfToken("diffuseColor"),
                                              pxr::SdfValueTypeNames->Color3f);
                if (auto it = matDesc.scalarChannels.find(MC::Diffuse);
                    it != matDesc.scalarChannels.end())
                    inp.Set(pxr::GfVec3f(it->second[0], it->second[1], it->second[2]));
                if (auto it = matDesc.textureChannels.find(MC::Diffuse);
                    it != matDesc.textureChannels.end())
                    inp.ConnectToSource(authorTex(it->second, matDesc.name + "_albedo_tex",
                                                  pxr::TfToken("rgb"),
                                                  pxr::SdfValueTypeNames->Color3f));
            }
            // metallic
            {
                auto inp = shader.CreateInput(pxr::TfToken("metallic"),
                                              pxr::SdfValueTypeNames->Float);
                if (auto it = matDesc.scalarChannels.find(MC::Metallic);
                    it != matDesc.scalarChannels.end())
                    inp.Set(it->second[0]);
                if (auto it = matDesc.textureChannels.find(MC::Metallic);
                    it != matDesc.textureChannels.end())
                    inp.ConnectToSource(authorTex(it->second, matDesc.name + "_metallic_tex",
                                                  pxr::TfToken("r"),
                                                  pxr::SdfValueTypeNames->Float));
            }
            // roughness
            {
                auto inp = shader.CreateInput(pxr::TfToken("roughness"),
                                              pxr::SdfValueTypeNames->Float);
                if (auto it = matDesc.scalarChannels.find(MC::Roughness);
                    it != matDesc.scalarChannels.end())
                    inp.Set(it->second[0]);
                if (auto it = matDesc.textureChannels.find(MC::Roughness);
                    it != matDesc.textureChannels.end())
                    inp.ConnectToSource(authorTex(it->second, matDesc.name + "_roughness_tex",
                                                  pxr::TfToken("r"),
                                                  pxr::SdfValueTypeNames->Float));
            }
            // specular
            if (auto it = matDesc.scalarChannels.find(MC::Specular);
                it != matDesc.scalarChannels.end())
                shader.CreateInput(pxr::TfToken("specular"), pxr::SdfValueTypeNames->Float)
                    .Set(it->second[0]);
            // emissiveColor
            if (auto scIt = matDesc.scalarChannels.find(MC::Emissive);
                scIt != matDesc.scalarChannels.end())
            {
                auto inp = shader.CreateInput(pxr::TfToken("emissiveColor"),
                                              pxr::SdfValueTypeNames->Color3f);
                inp.Set(pxr::GfVec3f(scIt->second[0], scIt->second[1], scIt->second[2]));
                if (auto it = matDesc.textureChannels.find(MC::Emissive);
                    it != matDesc.textureChannels.end())
                    inp.ConnectToSource(authorTex(it->second, matDesc.name + "_emissive_tex",
                                                  pxr::TfToken("rgb"),
                                                  pxr::SdfValueTypeNames->Color3f));
            }
            // normal (texture only)
            if (auto it = matDesc.textureChannels.find(MC::Normal);
                it != matDesc.textureChannels.end())
                shader.CreateInput(pxr::TfToken("normal"), pxr::SdfValueTypeNames->Normal3f)
                    .ConnectToSource(authorTex(it->second, matDesc.name + "_normal_tex",
                                               pxr::TfToken("rgb"),
                                               pxr::SdfValueTypeNames->Normal3f));
            // occlusion (texture only)
            if (auto it = matDesc.textureChannels.find(MC::AmbientOcclusion);
                it != matDesc.textureChannels.end())
                shader.CreateInput(pxr::TfToken("occlusion"), pxr::SdfValueTypeNames->Float)
                    .ConnectToSource(authorTex(it->second, matDesc.name + "_ao_tex",
                                               pxr::TfToken("r"),
                                               pxr::SdfValueTypeNames->Float));
            // opacity
            if (auto it = matDesc.scalarChannels.find(MC::Opacity);
                it != matDesc.scalarChannels.end())
            {
                shader.CreateInput(pxr::TfToken("opacity"), pxr::SdfValueTypeNames->Float)
                    .Set(it->second[0]);
                if (matDesc.opacityThreshold > 0.0f)
                    shader.CreateInput(pxr::TfToken("opacityThreshold"),
                                       pxr::SdfValueTypeNames->Float)
                        .Set(matDesc.opacityThreshold);
            }

            // Wire surface output
            mat.CreateSurfaceOutput().ConnectToSource(
                shader.ConnectableAPI(), pxr::TfToken("surface"));

            outMaterial = mat;
        }
    };
}
}
