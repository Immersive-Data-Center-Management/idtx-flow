#pragma once
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "idtxflow/types/TargetTypes.h"

namespace godot
{
    class Node3D;
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
        using OwningEntity = godot::Node3D;
    };

    static_assert(TargetEngineTypesLike<TargetEngineTypes<TargetEngineGodot>>, 
                  "Godot's engine types don't satisfy concept requirements");
}
}
