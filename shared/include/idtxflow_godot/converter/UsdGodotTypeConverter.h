#pragma once

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <pxr/base/tf/token.h>
#include <pxr/base/gf/matrix4d.h>

#include <idtxflow//types/TargetTypes.h>
#include <idtxflow/types/MaterialTypes.h>
#include <idtxflow/converter/TypeConverter.h>
#include <idtxflow/converter/AssetConverter.h>
#include <idtxflow/converter/MeshConverter.h>
#include <idtxflow_godot/types/GodotTypes.h>

namespace idtxflow
{
namespace converter
{
    template<>
    inline godot::Vector2 UsdTypeConverter<idtxflow::types::TargetEngineGodot>::toVector2(const class pxr::GfVec2f& usd_vec) {
        return godot::Vector2(usd_vec[0], usd_vec[1]);
    };

    template<>
    inline godot::Vector3 UsdTypeConverter<types::TargetEngineGodot>::toVector3(const class pxr::GfVec3d& usd_vec) {
        return godot::Vector3(
            static_cast<float>(usd_vec[0]), 
            static_cast<float>(usd_vec[1]),
            static_cast<float>(usd_vec[2]));
    };

    template<>
    inline godot::Vector4 UsdTypeConverter<types::TargetEngineGodot>::toVector4(const class pxr::GfVec4d& usd_vec) {
        return godot::Vector4(
            static_cast<float>(usd_vec[0]), 
            static_cast<float>(usd_vec[1]),
            static_cast<float>(usd_vec[2]),
            static_cast<float>(usd_vec[3]));
    };

    template<>
    inline godot::Quaternion UsdTypeConverter<types::TargetEngineGodot>::toQuaternion(const class pxr::GfQuatd& usd_quat)
    {
        const class pxr::GfVec3d vec = usd_quat.GetImaginary(); // (x,y,z)
        const double w = usd_quat.GetReal();
        godot::Quaternion quaternion(
            static_cast<float>(vec[0]),
            static_cast<float>(vec[1]),
            static_cast<float>(vec[2]),
            static_cast<float>(w));
        return quaternion.normalized();
    }

    template<>
    inline godot::Color UsdTypeConverter<types::TargetEngineGodot>::toColor(const class pxr::GfVec4f& usd_color) {
        return godot::Color(usd_color[0], usd_color[1], usd_color[2], usd_color[3]);
    };

    template<>
    inline godot::Transform3D UsdTypeConverter<idtxflow::types::TargetEngineGodot>::toTransform(
        const class pxr::GfMatrix4d& usd_transform, const class pxr::TfToken& spine_axis)
    {
        godot::Basis basis(
        godot::Vector3(static_cast<float>(usd_transform[0][0]), static_cast<float>(usd_transform[0][1]), static_cast<float>(usd_transform[0][2])),
        godot::Vector3(static_cast<float>(usd_transform[1][0]), static_cast<float>(usd_transform[1][1]), static_cast<float>(usd_transform[1][2])),
        godot::Vector3(static_cast<float>(usd_transform[2][0]), static_cast<float>(usd_transform[2][1]), static_cast<float>(usd_transform[2][2]))
        );

        godot::Vector3 origin(static_cast<float>(usd_transform[3][0]), static_cast<float>(usd_transform[3][1]), static_cast<float>(usd_transform[3][2]));

        if (spine_axis == pxr::UsdGeomTokens->x)
        {
            // rotate around the Z axis
            godot::Basis rot_z(godot::Vector3(0, 0, 1), static_cast<float>(godot::Math::deg_to_rad(90.0)));
            basis = basis * rot_z;
        } else if (spine_axis == pxr::UsdGeomTokens->y)
        {
            // nothing to do - matches the expected axis
        } else if (spine_axis == pxr::UsdGeomTokens->z)
        {
            // rotate around the X axis
            godot::Basis rot_x(godot::Vector3(1, 0, 0), static_cast<float>(godot::Math::deg_to_rad(90.0)));
            basis = basis * rot_x;
        }
        
        return godot::Transform3D(basis, origin);
    };

    template<>
    inline std::optional<godot::Ref<godot::Texture2D>> UsdTypeConverter<types::TargetEngineGodot>::toTexture(
        const std::vector<uint8_t>& binary_data, const std::string& lower_extension, const TexturePurpose purpose)
    {
        godot::Ref<godot::Image> img;
        img.instantiate();

        godot::String extension;
        godot::Error img_error = godot::ERR_FILE_UNRECOGNIZED;

        // convert the binary image data into a PackedByteArray for Godot
        godot::PackedByteArray buffer;
        buffer.resize(static_cast<int>(binary_data.size()));

        if (!binary_data .empty()) {
            std::memcpy(buffer.ptrw(), binary_data.data(), binary_data.size());
        }

        // 

        if (lower_extension == ".png") img_error = img->load_png_from_buffer(buffer);
        if (lower_extension == ".jpg" || lower_extension == ".jpeg") img_error = img->load_jpg_from_buffer(buffer);
        if (lower_extension == ".webp") img_error = img->load_webp_from_buffer(buffer);
        if (lower_extension == ".bmp") img_error = img->load_bmp_from_buffer(buffer);
        if (lower_extension == ".tga") img_error = img->load_tga_from_buffer(buffer);
        if (lower_extension == ".ktx" || lower_extension == ".ktx2") img_error = img->load_ktx_from_buffer(buffer);

        if (img_error != godot::OK || !img.is_valid())
        {
            godot::print_verbose("Unable to convert texture with extension '", lower_extension.c_str(), "' into Image. ErrorCode: ", static_cast<int>(img_error));
            return {};
        }

        godot::Ref<godot::Texture2D> texture_2d;
        
        
        return godot::ImageTexture::create_from_image(img);
    };
    
    inline godot::BaseMaterial3D::TextureChannel get_material_texture_channel(const class pxr::TfToken& token)
    {
        if (token == pxr::TfToken("r"))
            return godot::BaseMaterial3D::TEXTURE_CHANNEL_RED;
        if (token == pxr::TfToken("g"))
            return godot::BaseMaterial3D::TEXTURE_CHANNEL_GREEN;
        if (token == pxr::TfToken("b"))
            return godot::BaseMaterial3D::TEXTURE_CHANNEL_BLUE;
        if (token == pxr::TfToken("a"))
            return godot::BaseMaterial3D::TEXTURE_CHANNEL_ALPHA;
        
        return godot::BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE;
    }
    
    template<>
    inline std::optional<godot::Ref<godot::StandardMaterial3D>> UsdTypeConverter<types::TargetEngineGodot>::toMaterial(
        const types::MaterialDescription<godot::Ref<godot::Texture2D>>& material_description,
        const pxr::UsdStageRefPtr& stage)
    {
        using TextureType = types::TargetEngineTypes<types::TargetEngineGodot>::Texture;
        using TypeConverter = UsdTypeConverter<types::TargetEngineGodot>;
        
        godot::Ref<godot::StandardMaterial3D> standard_material;
        standard_material.instantiate();
        standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_DISABLED);
        
        for (const auto& channelData : material_description.channels)
	    {
            const types::MaterialChannelValue<TextureType>& channelValue = channelData.second;
		    switch (channelData.first)
		    {
		    case types::MaterialChannel::Diffuse:
			    {
				    if (auto texture = std::get_if<std::optional<types::TextureDescription<TextureType>>>(&channelValue); texture && texture->has_value())
				    {
					    const types::TextureDescription<TextureType>& textureDescription = texture->value();
					    if (auto godotTexture = UsdAssetConverter<types::TargetEngineGodot>::LoadTextureWithResolver(
					        textureDescription.filePath,
					        stage,
					        TexturePurpose::COLOR_TEXTURE))
						    standard_material->set_texture(godot::BaseMaterial3D::TEXTURE_ALBEDO, godotTexture.value());
				        // for diffuse textures use the scale as base color tint
				        godot::Color albedo = TypeConverter::toColor(textureDescription.scale);
				        albedo.a = standard_material->get_albedo().a; // keep a maybe already set alpha value
				        standard_material->set_albedo(albedo); 
				        // even though multiple textures may have different scaling and offset authored in USD, Godot does only support those values
				        // once for each material. So, the last channel providing those values will "win"
				        standard_material->set_uv1_scale(TypeConverter::toVector3(textureDescription.uvScale));
				        standard_material->set_uv1_offset(TypeConverter::toVector3(textureDescription.uvOffset));
				    } else if (auto const color = std::get_if<class pxr::GfVec3f>(&channelValue))
				    {
				        // when setting the albedo color from diffuse channel ensure we keep the existing alpha value
				        godot::Color albedo = TypeConverter::toColor(*color);
				        albedo.a = standard_material->get_albedo().a;
				        standard_material->set_albedo(albedo);
				    }
				    break;
			    }
		    case types::MaterialChannel::Metallic:
			    {
				    if (auto texture = std::get_if<std::optional<types::TextureDescription<TextureType>>>(&channelValue); texture && texture->has_value())
				    {
					    const types::TextureDescription<TextureType>& textureDescription = texture->value();
				        if (auto godotTexture = UsdAssetConverter<types::TargetEngineGodot>::LoadTextureWithResolver(
				            textureDescription.filePath,
				            stage,
				            TexturePurpose::METALLIC_TEXTURE))
				            standard_material->set_texture(godot::BaseMaterial3D::TEXTURE_METALLIC, godotTexture.value());

				        // even though multiple textures may have different scaling and offset authored in USD, Godot does only support those values
				        // once for each material. So, the last channel providing those values will "win"
				        standard_material->set_uv1_scale(TypeConverter::toVector3(textureDescription.uvScale));
				        standard_material->set_uv1_offset(TypeConverter::toVector3(textureDescription.uvOffset));
				        
					    if (textureDescription.channel != pxr::TfToken())
						    standard_material->set_metallic_texture_channel(get_material_texture_channel(textureDescription.channel));
				    	// set the default metallic multiplier to 1.0 to ensure the metallic texture takes affect
				    	if (standard_material->get_metallic() == 0.0f)
				    	{
				    		standard_material->set_metallic(1.0f);
				    	}
				    } else if (auto const metallic = std::get_if<float>(&channelValue))
				    {
					    standard_material->set_metallic(*metallic);
				    }
				    break;
			    }
		    case types::MaterialChannel::Roughness:
			    {
				    if (auto texture = std::get_if<std::optional<types::TextureDescription<TextureType>>>(&channelValue); texture && texture->has_value())
				    {
					    const types::TextureDescription<TextureType>& textureDescription = texture->value();
				        if (auto godotTexture = UsdAssetConverter<types::TargetEngineGodot>::LoadTextureWithResolver(
				            textureDescription.filePath,
				            stage,
				            TexturePurpose::ROUGHNESS_TEXTURE))
				            standard_material->set_texture(godot::BaseMaterial3D::TEXTURE_ROUGHNESS, godotTexture.value());

				        // even though multiple textures may have different scaling and offset authored in USD, Godot does only support those values
				        // once for each material. So, the last channel providing those values will "win"
				        standard_material->set_uv1_scale(TypeConverter::toVector3(textureDescription.uvScale));
				        standard_material->set_uv1_offset(TypeConverter::toVector3(textureDescription.uvOffset));
				        
				        if (textureDescription.channel != pxr::TfToken())
				            standard_material->set_roughness_texture_channel(get_material_texture_channel(textureDescription.channel));
				    	// ensure the roughness multiplier is set to 1.0 to ensure the texture values are taken as is
				    	if (standard_material->get_roughness() == 0.0f)
				    	{
				    		standard_material->set_roughness(1.0f);
				    	}
				    } else if (auto const roughness = std::get_if<float>(&channelValue))
				    {
				        standard_material->set_roughness(*roughness);
				    }
				    break;
			    }
		    case types::MaterialChannel::Specular:
			    {
				    if (auto const specular = std::get_if<float>(&channelValue))
				    {
					    standard_material->set_specular(*specular);
				    }
				    break;
			    }
		    case types::MaterialChannel::Emissive:
			    {
		            standard_material->set_feature(godot::BaseMaterial3D::FEATURE_EMISSION, true);
				    if (auto texture = std::get_if<std::optional<types::TextureDescription<TextureType>>>(&channelValue); texture && texture->has_value())
				    {
					    const types::TextureDescription<TextureType>& textureDescription = texture->value();
				        if (auto godotTexture = UsdAssetConverter<types::TargetEngineGodot>::LoadTextureWithResolver(
				            textureDescription.filePath,
				            stage,
				            TexturePurpose::COLOR_TEXTURE))
				            standard_material->set_texture(godot::BaseMaterial3D::TEXTURE_EMISSION, godotTexture.value());
				        
				        // even though multiple textures may have different scaling and offset authored in USD, Godot does only support those values
				        // once for each material. So, the last channel providing those values will "win"
				        standard_material->set_uv1_scale(TypeConverter::toVector3(textureDescription.uvScale));
				        standard_material->set_uv1_offset(TypeConverter::toVector3(textureDescription.uvOffset));
				    } else if (auto const emissive = std::get_if<class pxr::GfVec3f>(&channelValue))
				    {
					    standard_material->set_emission(TypeConverter::toColor(*emissive));
				    }
				    break;
			    }
		    case types::MaterialChannel::Normal:
			    {
		            standard_material->set_feature(godot::BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
				    if (auto texture = std::get_if<std::optional<types::TextureDescription<TextureType>>>(&channelValue); texture && texture->has_value())
				    {
					    const types::TextureDescription<TextureType>& textureDescription = texture->value();
				        if (auto godotTexture = UsdAssetConverter<types::TargetEngineGodot>::LoadTextureWithResolver(
				            textureDescription.filePath,
				            stage,
				            TexturePurpose::NORMAL_TEXTURE))
				            standard_material->set_texture(godot::BaseMaterial3D::TEXTURE_NORMAL, godotTexture.value());

				        // even though multiple textures may have different scaling and offset authored in USD, Godot does only support those values
				        // once for each material. So, the last channel providing those values will "win"
				        standard_material->set_uv1_scale(TypeConverter::toVector3(textureDescription.uvScale));
				        standard_material->set_uv1_offset(TypeConverter::toVector3(textureDescription.uvOffset));
				    }
				    break;
			    }
		    case types::MaterialChannel::AmbientOcclusion:
			    {
		            standard_material->set_feature(godot::BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, true);
				    if (auto texture = std::get_if<std::optional<types::TextureDescription<TextureType>>>(&channelValue); texture && texture->has_value())
				    {
					    const types::TextureDescription<TextureType>& textureDescription = texture->value();
				        if (auto godotTexture = UsdAssetConverter<types::TargetEngineGodot>::LoadTextureWithResolver(
				            textureDescription.filePath,
				            stage,
				            TexturePurpose::COLOR_TEXTURE))
				            standard_material->set_texture(godot::BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, godotTexture.value());

				        // even though multiple textures may have different scaling and offset authored in USD, Godot does only support those values
				        // once for each material. So, the last channel providing those values will "win"
				        standard_material->set_uv1_scale(TypeConverter::toVector3(textureDescription.uvScale));
				        standard_material->set_uv1_offset(TypeConverter::toVector3(textureDescription.uvOffset));
				        
				        if (textureDescription.channel != pxr::TfToken())
				            standard_material->set_ao_texture_channel(get_material_texture_channel(textureDescription.channel));
				    }
				    break;
			    }
		    case types::MaterialChannel::Opacity:
			    {
		            if (material_description.opacityThreshold > 0.0)
		            {
		                standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		                standard_material->set_alpha_scissor_threshold(material_description.opacityThreshold);
		            } else
		            {
		                if (auto const opacity = std::get_if<float>(&channelValue))
		                {
		                    // apply the opacity as alpha channel to the existing albedo color
		                    if (*opacity < 1.0)
		                    {
		                        standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
		                        godot::Color albedo = standard_material->get_albedo();
		                        albedo.a = *opacity;
		                        standard_material->set_albedo(albedo);
		                    }
		                }
		            }
		            break;
			    }
		    case types::MaterialChannel::ClearCoat:
		        {
		            standard_material->set_feature(godot::BaseMaterial3D::FEATURE_CLEARCOAT, true);
		            if (auto texture = std::get_if<std::optional<types::TextureDescription<TextureType>>>(&channelValue); texture && texture->has_value())
		            {
		                const types::TextureDescription<TextureType>& textureDescription = texture->value();
		                if (auto godotTexture = UsdAssetConverter<types::TargetEngineGodot>::LoadTextureWithResolver(
		                    textureDescription.filePath,
		                    stage,
		                    TexturePurpose::COLOR_TEXTURE))
		                    standard_material->set_texture(godot::BaseMaterial3D::TEXTURE_CLEARCOAT, godotTexture.value());

		                // even though multiple textures may have different scaling and offset authored in USD, Godot does only support those values
		                // once for each material. So, the last channel providing those values will "win"
		                standard_material->set_uv1_scale(TypeConverter::toVector3(textureDescription.uvScale));
		                standard_material->set_uv1_offset(TypeConverter::toVector3(textureDescription.uvOffset));
		            } else if (auto const clearcoat = std::get_if<float>(&channelValue))
		            {
		                standard_material->set_clearcoat(*clearcoat);
		            }
		            break;
		        }
		    default:
			    break;
		    }
	    }
        
        return standard_material;
    }

    template<>
    inline godot::Vector2 UsdMeshConverter<idtxflow::types::TargetEngineGodot>::FlipUvV(const godot::Vector2& input)
    {
        return {
            input.x, -input.y
        };
    }

    template<>
    class TargetMeshBuilder<idtxflow::types::TargetEngineGodot>
    {
    public:
        using Types = idtxflow::types::TargetEngineTypes<idtxflow::types::TargetEngineGodot>;
        
        void AddVertex(Types::MeshData& meshData, 
                          const Types::Vector3& position,
                          const Types::Vector3& normal,
                          const Types::Vector2& uv,
                          const std::vector<uint32_t>& bones,
                          const std::vector<float>& boneWeights)
        {
            meshData.Vertices.push_back(position);
            meshData.Normals.push_back(normal);
            meshData.UVs.push_back(uv);

            if (!bones.empty())
            {
                // we need to always push 4 or 8 entries into the bones & weight array for each vertex in Godot.
                // As Godot takes the bone weights as-is we normalize those entries, so the weights would not sum up
								// to 1.0 to prevent skinning artifacts.
                if (boneWeights.size() > 4)
                {
                    meshData.boneWeightCount = meshData.BONEWEIGHT_COUNT_8;
                }
                
                float weightSum = 0.0;
                uint32_t maxBoneWeightCount = static_cast<uint32_t>(meshData.boneWeightCount);
                for (size_t i = 0; i < maxBoneWeightCount; ++i)
                {
                    if (i < bones.size())
                    {
                        meshData.Bones.push_back(bones[i]);
                        meshData.Weights.push_back(boneWeights[i]);
                        weightSum += boneWeights[i];
                    } else
                    {
                        meshData.Bones.push_back(0);
                        meshData.Weights.push_back(0.0f);
                    }
                }

                // normalize the boneWeights
                int64_t boneIndex = meshData.Bones.size() - maxBoneWeightCount;
                for (size_t i = 0; i < maxBoneWeightCount; ++i)
                {
                    meshData.Weights[boneIndex + i] /= weightSum;
                }
            }
        }

        Types::Index GetVertexCount(const Types::MeshData& meshData)
        {
            return meshData.Vertices.size();
        }

        void AddIndex(Types::MeshData& mesh_data, Types::Index index)
        {
            mesh_data.Triangles.push_back(static_cast<int32_t>(index));
        }
    };
}
}