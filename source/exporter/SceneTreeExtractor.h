#pragma once

/**
 * @file SceneTreeExtractor.h
 * @brief Walks a Godot Node3D sub-tree and produces neutral NodeExportDescription structs.
 *
 * This is the Godot-specific "reading" side of the exporter: it is the only place that touches
 * godot:: scene-tree objects. It classifies each node (Xform / Mesh / Skeleton), reads its local
 * transform, copies USD round-trip metadata from IUsdNode3D, reads ArrayMesh surface geometry
 * (Phase 2), and extracts StandardMaterial3D into neutral MaterialExportDescription (Phase 3).
 *
 * See docs/EXPORTER_DESIGN.md §5 for the full extraction rules.
 */

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/object.hpp>

#include <idtxflow/exporter/ExportDescription.h>
#include <idtxflow/exporter/UsdStageExporter.h>
#include <idtxflow_godot/exporter/GodotFromTypeConverter.h>
#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include <idtxflow_godot/types/GodotTypes.h>

namespace idtxflow_godot
{
    class SceneTreeExtractor
    {
    public:
        using NodeDesc = idtxflow::exporter::NodeExportDescription<idtxflow::types::TargetEngineGodot>;

        /** Build a neutral description tree rooted at `root` (which becomes the root prim). */
        static NodeDesc Extract(godot::Node3D* root)
        {
            NodeDesc desc;
            ExtractRecursive(root, desc);
            return desc;
        }

    private:
        /** Fill `out` from a single node and recurse into its Node3D children. */
        static void ExtractRecursive(godot::Node3D* node, NodeDesc& out)
        {
            using FromConverter = idtxflow::exporter::FromTypeConverter<idtxflow::types::TargetEngineGodot>;

            // Local transform (relative to parent) is exactly what USD's xformOp:transform expects.
            out.localTransform = FromConverter::fromTransform(node->get_transform());

            // If the node was imported from USD, reuse its original prim name/path/type for round-tripping.
            if (IUsdNode3D* usd_node = IUsdNode3D::from_node(node))
            {
                const godot::String node_name = node->get_name();
                const godot::String prim_name = usd_node->get_prim_name();
                out.primName = idtxflow::exporter::SanitizePrimName(
                    (prim_name.is_empty() ? node_name : prim_name).utf8().get_data());

                const godot::String prim_path = usd_node->get_prim_path();
                if (!prim_path.is_empty()) out.originalPrimPath = prim_path.utf8().get_data();

                const godot::String prim_type = usd_node->get_prim_type();
                if (!prim_type.is_empty()) out.originalPrimType = prim_type.utf8().get_data();
            }
            else
            {
                out.primName = idtxflow::exporter::SanitizePrimName(
                    godot::String(node->get_name()).utf8().get_data());
            }

            // Classify the node kind. cast_to also matches the Usd* subclasses of these Godot types.
            if (auto* mi = godot::Object::cast_to<godot::MeshInstance3D>(node))
            {
                out.kind = NodeDesc::Kind::Mesh;

                // Read ArrayMesh surfaces into neutral MeshData (Phase 2).
                // surfaceMaterials entries are left nullopt here; Phase 3 fills them.
                godot::Ref<godot::Mesh> base_mesh = mi->get_mesh();
                if (base_mesh.is_valid())
                {
                    const int surf_count = base_mesh->get_surface_count();
                    out.surfaces.reserve(surf_count);
                    out.surfaceMaterials.resize(surf_count, std::nullopt);

                    for (int s = 0; s < surf_count; ++s)
                    {
                        // surface_get_arrays returns an Array of ARRAY_MAX slots; unused slots are NIL.
                        // Explicit PackedXxxArray casts: NIL Variant -> empty array (same pattern as
                        // Dictionary::get in UsdExporter.cpp).
                        godot::Array a = base_mesh->surface_get_arrays(s);
                        idtxflow::types::MeshData md;
                        md.Vertices     = godot::PackedVector3Array(a[godot::Mesh::ARRAY_VERTEX]);
                        md.Triangles    = godot::PackedInt32Array(a[godot::Mesh::ARRAY_INDEX]);
                        md.Normals      = godot::PackedVector3Array(a[godot::Mesh::ARRAY_NORMAL]);
                        md.UVs          = godot::PackedVector2Array(a[godot::Mesh::ARRAY_TEX_UV]);
                        md.VertexColors = godot::PackedColorArray(a[godot::Mesh::ARRAY_COLOR]);
                        out.surfaces.push_back(std::move(md));

                        // Phase 3: extract StandardMaterial3D into a neutral MaterialExportDescription.
                        // Priority (matches Godot's render priority):
                        //   1. GeometryInstance3D::material_override  — single slot that overrides ALL surfaces
                        //   2. MeshInstance3D::surface_override_material(s) — per-surface override
                        //   3. Mesh resource's own material for that surface
                        godot::Ref<godot::Material> mat_ref = mi->get_material_override();  // (1)
                        if (!mat_ref.is_valid()) mat_ref = mi->get_surface_override_material(s);  // (2)
                        if (!mat_ref.is_valid()) mat_ref = base_mesh->surface_get_material(s);   // (3)
                        if (auto* std_mat = godot::Object::cast_to<godot::StandardMaterial3D>(*mat_ref))
                        {
                            const std::string mat_base = idtxflow::exporter::SanitizePrimName(
                                (godot::String(std_mat->get_name()).is_empty()
                                    ? (godot::String(node->get_name()) + "_mat" + godot::String::num_int64(s))
                                    : godot::String(std_mat->get_name())).utf8().get_data());
                            out.surfaceMaterials[s] = ExtractMaterial(std_mat, mat_base);
                        }
                    }
                }
            }
            else if (auto* sk = godot::Object::cast_to<godot::Skeleton3D>(node))
            {
                (void)sk;  // suppress unused-variable warning until Phase 4
                out.kind = NodeDesc::Kind::Skeleton;
                // TODO(phase 4): read Skeleton3D bones.
            }
            else
            {
                out.kind = NodeDesc::Kind::Xform;
            }

            // Recurse into direct Node3D children only (non-spatial nodes are skipped for now).
            const int child_count = node->get_child_count();
            for (int i = 0; i < child_count; ++i)
            {
                if (godot::Node3D* child = godot::Object::cast_to<godot::Node3D>(node->get_child(i)))
                {
                    NodeDesc child_desc;
                    ExtractRecursive(child, child_desc);

                    // De-duplicate sibling prim names. primName is already sanitized by ExtractRecursive;
                    // if two siblings map to the same identifier, append _1, _2, … until unique.
                    // Linear scan is fine — typical scene depths and sibling counts are small.
                    const std::string base_name = child_desc.primName;
                    int suffix = 1;
                    for (bool collision = true; collision; )
                    {
                        collision = false;
                        for (const NodeDesc& existing : out.children)
                        {
                            if (existing.primName == child_desc.primName)
                            {
                                child_desc.primName = base_name + "_" + std::to_string(suffix++);
                                collision = true;
                                break;
                            }
                        }
                    }

                    out.children.push_back(std::move(child_desc));
                }
            }
        }

        // --- Material extraction helpers ---

        /**
         * Extract a PNG image from a Texture2D into raw bytes, ready for the shared exporter to
         * write to disk. Returns an empty vector if the image is unavailable or cannot be encoded.
         */
        static std::vector<uint8_t> ExtractImageBytes(const godot::Ref<godot::Texture2D>& tex)
        {
            if (!tex.is_valid()) return {};
            godot::Ref<godot::Image> img = tex->get_image();
            if (!img.is_valid()) return {};
            // Decompress if needed (e.g. S3TC/ETC compressed import textures).
            if (img->is_compressed())
                img->decompress();
            godot::PackedByteArray png = img->save_png_to_buffer();
            return std::vector<uint8_t>(png.ptr(), png.ptr() + png.size());
        }

        /** Build a TextureExportRef from a Texture2D, using `baseName` as the output filename stem. */
        static idtxflow::exporter::TextureExportRef ExtractTexture(
            const godot::Ref<godot::Texture2D>& tex,
            const std::string& baseName,
            const pxr::GfVec2f& uvScale  = pxr::GfVec2f(1, 1),
            const pxr::GfVec2f& uvOffset = pxr::GfVec2f(0, 0))
        {
            idtxflow::exporter::TextureExportRef ref;
            ref.sourceImagePath = baseName + ".png";
            ref.extension       = ".png";
            ref.uvScale         = uvScale;
            ref.uvOffset        = uvOffset;
            ref.imageBytes      = ExtractImageBytes(tex);
            return ref;
        }

        /**
         * Extract a StandardMaterial3D into a neutral MaterialExportDescription.
         * Scalar channel values and optional texture bytes are stored; the shared exporter
         * authors USD UsdPreviewSurface + UsdUVTexture and writes the texture files.
         */
        static idtxflow::exporter::MaterialExportDescription ExtractMaterial(
            godot::StandardMaterial3D* mat, const std::string& matBaseName)
        {
            using MC = idtxflow::types::MaterialChannel;
            idtxflow::exporter::MaterialExportDescription desc;
            desc.name = idtxflow::exporter::SanitizePrimName(matBaseName);

            const pxr::GfVec2f uvScale(mat->get_uv1_scale().x, mat->get_uv1_scale().y);
            const pxr::GfVec2f uvOffset(mat->get_uv1_offset().x, mat->get_uv1_offset().y);

            // Diffuse / albedo
            {
                const godot::Color c = mat->get_albedo();
                desc.scalarChannels[MC::Diffuse] = pxr::GfVec4f(c.r, c.g, c.b, c.a);
                godot::Ref<godot::Texture2D> tex = mat->get_texture(godot::BaseMaterial3D::TEXTURE_ALBEDO);
                if (tex.is_valid())
                    desc.textureChannels[MC::Diffuse] = ExtractTexture(tex, desc.name + "_albedo", uvScale, uvOffset);
            }
            // Metallic
            {
                desc.scalarChannels[MC::Metallic] = pxr::GfVec4f(mat->get_metallic(), 0, 0, 0);
                godot::Ref<godot::Texture2D> tex = mat->get_texture(godot::BaseMaterial3D::TEXTURE_METALLIC);
                if (tex.is_valid())
                    desc.textureChannels[MC::Metallic] = ExtractTexture(tex, desc.name + "_metallic", uvScale, uvOffset);
            }
            // Roughness
            {
                desc.scalarChannels[MC::Roughness] = pxr::GfVec4f(mat->get_roughness(), 0, 0, 0);
                godot::Ref<godot::Texture2D> tex = mat->get_texture(godot::BaseMaterial3D::TEXTURE_ROUGHNESS);
                if (tex.is_valid())
                    desc.textureChannels[MC::Roughness] = ExtractTexture(tex, desc.name + "_roughness", uvScale, uvOffset);
            }
            // Specular
            desc.scalarChannels[MC::Specular] = pxr::GfVec4f(mat->get_specular(), 0, 0, 0);
            // Emissive — emission color/texture; non-black color implies emission is active
            {
                const godot::Color e = mat->get_emission();
                if (e.r > 0.f || e.g > 0.f || e.b > 0.f)
                    desc.scalarChannels[MC::Emissive] = pxr::GfVec4f(e.r, e.g, e.b, 0);
                godot::Ref<godot::Texture2D> tex = mat->get_texture(godot::BaseMaterial3D::TEXTURE_EMISSION);
                if (tex.is_valid())
                    desc.textureChannels[MC::Emissive] = ExtractTexture(tex, desc.name + "_emissive", uvScale, uvOffset);
            }
            // Normal map — present if the texture slot is non-null
            {
                godot::Ref<godot::Texture2D> tex = mat->get_texture(godot::BaseMaterial3D::TEXTURE_NORMAL);
                if (tex.is_valid())
                    desc.textureChannels[MC::Normal] = ExtractTexture(tex, desc.name + "_normal", uvScale, uvOffset);
            }
            // Ambient occlusion — present if the texture slot is non-null
            {
                godot::Ref<godot::Texture2D> tex = mat->get_texture(godot::BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION);
                if (tex.is_valid())
                    desc.textureChannels[MC::AmbientOcclusion] = ExtractTexture(tex, desc.name + "_ao", uvScale, uvOffset);
            }
            // Opacity / transparency
            {
                const auto transparency = mat->get_transparency();
                if (transparency == godot::BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR)
                {
                    desc.opacityThreshold = mat->get_alpha_scissor_threshold();
                    desc.scalarChannels[MC::Opacity] = pxr::GfVec4f(1.0f, 0, 0, 0);
                }
                else if (transparency != godot::BaseMaterial3D::TRANSPARENCY_DISABLED)
                {
                    const float alpha = mat->get_albedo().a;
                    desc.scalarChannels[MC::Opacity] = pxr::GfVec4f(alpha, 0, 0, 0);
                }
            }
            return desc;
        }
    };
}
