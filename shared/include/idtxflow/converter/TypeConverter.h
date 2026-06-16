#pragma once

/**
 * @file TypeConverter.h
 * @brief Converter for basic USD types into their respective TargetEngine specific representation.
 * 
 * This includes basic math types like vectors, colors and transforms, but also more complex types like textures and materials.
 */

#include <optional>

#include <pxr/usd/usd/stage.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix4d.h>

#include "../types/TargetTypes.h"
#include "../types/MaterialTypes.h"

namespace idtxflow
{
namespace converter
{
	// The texture purpose can be passed from the material converstion specialization
	// to the texture loading to allow treating texture data differently based on theire intended usage
	enum TexturePurpose
	{
		COLOR_TEXTURE,
		NORMAL_TEXTURE,
		ROUGHNESS_TEXTURE,
		METALLIC_TEXTURE,
	};
	
	template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
	class UsdTypeConverter
	{
	public:
		using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;

		// those methods are required to be specialized for the individual target engine
		inline static typename Types::Vector2 toVector2(const class pxr::GfVec2d& usdVec);
		inline static typename Types::Vector2 toVector2(const class pxr::GfVec2f& usdVec)
		{
			return toVector2(pxr::GfVec2d(usdVec));
		}
		inline static typename Types::Vector2 toVector2(const class pxr::GfVec2h& usdVec)
		{
			return toVector2(pxr::GfVec2d(usdVec));
		}
		inline static typename Types::Vector3 toVector3(const class pxr::GfVec3d& usdVec);
		inline static typename Types::Vector3 toVector3(const class pxr::GfVec3f& usdVec)
		{
			return toVector3(pxr::GfVec3d(usdVec));
		}
		inline static typename Types::Vector3 toVector3(const class pxr::GfVec3h& usdVec)
		{
			return toVector3(pxr::GfVec3d(usdVec));
		}
		inline static typename Types::Vector3 toVector3(const class pxr::GfVec2f& usdVec)
		{
			return toVector3(pxr::GfVec3d(usdVec[0], usdVec[1], 0.0));
		}
	    inline static typename Types::Vector4 toVector4(const class pxr::GfVec4d& usdVec);
		inline static typename Types::Vector4 toVector4(const class pxr::GfVec4f& usdVec)
		{
		    return toVector4(pxr::GfVec4d(usdVec[0], usdVec[1], usdVec[2], usdVec[3]));
		};
	    inline static typename Types::Quaternion toQuaternion(const class pxr::GfQuatd& usdQuat);
	    inline static typename Types::Quaternion toQuaternion(const class pxr::GfQuatf& usdQuat)
	    {
	        return toQuaternion(pxr::GfQuatd(usdQuat));
	    }
		inline static typename Types::Color toColor(const class pxr::GfVec4f& usdColor);
		inline static typename Types::Color toColor(const class pxr::GfVec3f& usdColor)
		{
			return toColor(pxr::GfVec4f(usdColor[0], usdColor[1], usdColor[2], 0.0));
		}

		/**
		 * The specialized implementation should create the game engine specific transform matrix representation.
		 * The transform should be taken "as-is". Adjustments for coordinate-system differeces like handedness, or
		 * what is used as "up" axis will be done kind of globally for a whole converted usd stage and not on individual
		 * prims.
		 * @param usdTransform 
		 * @param spineAxis Some prims (Cone, Cylinder) author a spine axis they "grow" along. The representation of
		 *                  those simple prims may differ in game engines. Thus we pass the spine axis to the transform
		 *                  conversion, that could be used to properly rotate/align the prim along this axis.
		 * @return 
		 */
		inline static typename Types::Transform toTransform(
		    const class pxr::GfMatrix4d& usdTransform, const class pxr::TfToken& spineAxis = pxr::TfToken());

		/**
		 * The specialized implementation of this function should create the TargetEngine specific representation
		 * of a texture object based on the provided raw image data.
		 * @param binaryData 
		 * @param lower_extension The lower case file extension including the leading "."
		 *                        For "image.png" the extension passed will be ".png". 
		 * @return 
		 */
		inline static std::optional<typename Types::Texture> toTexture(
		    const std::vector<uint8_t>& binaryData, const std::string& lower_extension, const TexturePurpose purpose);
	    
	    inline static std::optional<typename Types::Material> toMaterial(
	        const types::MaterialDescription<typename Types::Texture>& materialDescription,
	        const pxr::UsdStageRefPtr& stage);
	
	};

	template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
	class TargetMeshBuilder
	{
	public:
		using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
		using MeshDataType = typename Types::MeshData;

		TargetMeshBuilder() = default;
		virtual ~TargetMeshBuilder() = default;
		virtual void AddVertex(MeshDataType& mesh, 
							  const typename Types::Vector3& position,
							  const typename Types::Vector3& normal,
							  const typename Types::Vector2& uv,
							  const std::vector<uint32_t>& boneIdx = {},
							  const std::vector<float>& boneWeight = {}) = 0;

		virtual typename Types::Index GetVertexCount(const MeshDataType& meshData) = 0;
		virtual void AddIndex(MeshDataType& meshData, typename Types::Index index) = 0;
	};	
}
}
