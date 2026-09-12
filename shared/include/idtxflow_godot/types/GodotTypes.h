#pragma once
#include <string>
#include <vector>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "idtxflow/types/TargetTypes.h"

namespace godot
{
    class Texture2D;
    class StandardMaterial3D;
}

namespace idtxflow
{
namespace types
{
    // define Godot as target engine
    struct TargetEngineGodot {
        static constexpr const char* name = "Godot";
    };

    // A single blend shape (morph target) belonging to a mesh section. The delta
    // arrays are index-aligned with the section's Vertices array (one entry per
    // engine vertex). Godot's RELATIVE blend-shape mode adds weight * delta to the
    // base geometry, so these hold the raw per-vertex OFFSETS (not base + delta):
    // storing absolute positions here would add an extra weight * base term and
    // blow the mesh apart. When the base surface has normals (it always does after
    // conversion), nrm_deltas is sized to match and zero-filled where the shape
    // authored no normal offsets -- Godot rejects a surface whose blend shapes do
    // not carry the same Vertex/Normal arrays as the base.
    struct BlendShapeData
    {
        std::string name;
        float weight = 0.0f;
        // In-between support: an in-between is promoted to its own shape at
        // `position` on its primary's weight axis; the primary itself carries
        // position 1 and an empty `primary`.
        float position = 1.0f;
        std::string primary;
        bool has_normals = false;
        godot::PackedVector3Array pos_deltas;   // size == Vertices.size()
        godot::PackedVector3Array nrm_deltas;   // size == Vertices.size()
    };

    struct MeshData
    {
        enum BoneWeightCount : uint32_t
        {
            BONEWEIGHT_COUNT_4 = 4,
            BONEWEIGHT_COUNT_8 = 8,
        };

        godot::PackedVector3Array Vertices;
        godot::PackedInt32Array Triangles;
        godot::PackedVector3Array Normals;
        godot::PackedVector2Array UVs;
        godot::PackedColorArray VertexColors;
        godot::PackedInt32Array Bones;
        godot::PackedFloat32Array Weights;
        // Blend shapes (morph targets) authored on the mesh's UsdSkelBlendShape
        // targets, densified onto this section's engine vertices. Empty for meshes
        // without blend shapes.
        std::vector<BlendShapeData> BlendShapes;
        // to be able to create the mesh array surface with the correct bone weight count we store this information here
        // default bone weight count per vertex is 4
        BoneWeightCount boneWeightCount = BONEWEIGHT_COUNT_4;
    };

    template<>
    struct TargetEngineTypes<TargetEngineGodot>
    {
        using Vector4 = godot::Vector4;
        using Vector3 = godot::Vector3;
        using Vector2 = godot::Vector2;
        using Quaternion = godot::Quaternion;
        using Color = godot::Color;
        using Transform = godot::Transform3D;
        using MeshData = MeshData;
        using Index = size_t;

        using Material = godot::Ref<godot::StandardMaterial3D>;
        using Texture = godot::Ref<godot::Texture2D>;

        using ConvertedEntity = godot::Node3D;
        // A godot-cpp Object wrapper belongs to the GDExtension DLL that created
        // it and must not be dereferenced by another DLL. Godot's ObjectID is a
        // stable POD value that can safely cross that boundary.
        using ConvertedEntityHandle = uint64_t;
        using OwningEntity = godot::Node3D;

        /**
         * Create a cross-module handle for an entity using the caller's local
         * godot-cpp wrapper. Call this in the module that owns the entity.
         */
        static ConvertedEntityHandle GetConvertedEntityHandle(ConvertedEntity* entity) noexcept
        {
            return entity ? entity->get_instance_id() : 0;
        }

        /**
         * Resolve a handle through the caller's godot-cpp instance-binding
         * token. The returned wrapper is therefore local to the calling DLL.
         */
        static ConvertedEntity* ResolveConvertedEntity(ConvertedEntityHandle handle) noexcept
        {
            if (handle == 0) return nullptr;
            godot::Object* object = godot::ObjectDB::get_instance(handle);
            return godot::Object::cast_to<godot::Node3D>(object);
        }
    };

    static_assert(TargetEngineTypesLike<TargetEngineTypes<TargetEngineGodot>>, 
                  "Godot's engine types don't satisfy concept requirements");
}
}
