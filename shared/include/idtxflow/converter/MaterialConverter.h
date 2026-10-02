#pragma once

/**
 * @file MaterialConverter.h
 * @brief Converter for UsdShadeMaterial prims into a generic MaterialDescription format that can be further processed
 * into engine-specific material assets.
 *
 * The conversion does not support shader node graphs, but only simple one-shader materials
 * with direct texture or value connections to the main shader node.
 *
 * If a UsdShadeMaterial is linked to an MDL shader asset, the conversion will be handled by the
 * UsdMdlMaterialConverter.
 */
#include <optional>

#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdShade/material.h>

#include "../types/TargetTypes.h"
#include "../types/MaterialTypes.h"
#ifdef IDTXFLOW_MDL_ENABLED
#include "MdlMaterialConverter.h"
#endif
namespace idtxflow
{
namespace converter
{
template <typename TargetEngine>
    requires idtxflow::types::ValidTargetEngine<TargetEngine>
class UsdMaterialConverter
{
  public:
    using TargetTypes = idtxflow::types::TargetEngineTypes<TargetEngine>;
    using TextureType = typename TargetTypes::Texture;

    /**
     * Convert an UsdShadeMaterial prim into game engine specific data structures. It is the responsibility
     * of the calling implementation to prevent repetitive conversion of the same material to save performance
     * and resources during the conversion process.
     * @param material
     * @return
     */
    std::optional<types::MaterialDescription<TextureType>> Convert(const pxr::UsdShadeMaterial& usdMaterial)
    {
        if (!usdMaterial) return {};

#ifdef IDTXFLOW_MDL_ENABLED
        // if the material is linked to a MDL shader asset file, try to convert the MDL shader into a Godot material
        for (const pxr::UsdShadeOutput& shadeOutput: usdMaterial.GetSurfaceOutputs())
        {
            if (shadeOutput.GetBaseName() == pxr::TfToken("mdl:surface"))
            {
                return UsdMdlMaterialConverter<TargetEngine>().Convert(usdMaterial, shadeOutput);
            }
        }
#endif
        // convert the actual shade material based on the surface shader authored
        pxr::UsdShadeOutput shadeOutput = usdMaterial.GetSurfaceOutput();
        if (!shadeOutput) return {};

        pxr::UsdShadeOutput::SourceInfoVector shadeSourceInfos = shadeOutput.GetConnectedSources();
        if (shadeSourceInfos.empty()) return {};

        // usually there is only one (preview) source connected to the shader actually providing how surface
        // will appear.
        pxr::UsdShadeConnectableAPI shader = shadeSourceInfos[0].source;
        if (!shader) return {};

        // with access to the actual shader we can extract all relevant information to build up a material
        // each material does have several channels that influence the final appearance of the same. We will
        // extract the data based on the channel

        types::MaterialDescription<TextureType> material;
        material.id = usdMaterial.GetPath().GetString();

        // 1. Diffuse Color / Basic Color
        if (pxr::UsdShadeInput diffuseInput = shader.GetInput(pxr::TfToken("diffuseColor")))
        {
            // diffuse color could be a single value or a referenced texture
            if (diffuseInput.HasConnectedSource())
            {
                material.channels.emplace(types::MaterialChannel::Diffuse, GetConnectedTexture(diffuseInput));
            }
            else
            {
                class pxr::GfVec3f diffuse;
                if (diffuseInput.Get(&diffuse)) material.channels.emplace(types::MaterialChannel::Diffuse, diffuse);
            }
        }

        // 2. Metallic
        if (pxr::UsdShadeInput metallicInput = shader.GetInput(pxr::TfToken("metallic")))
        {
            // diffuse color could be a single value or a referenced texture
            if (metallicInput.HasConnectedSource())
            {
                material.channels.emplace(types::MaterialChannel::Metallic, GetConnectedTexture(metallicInput));
            }
            else
            {
                class pxr::GfVec3f metallic;
                if (metallicInput.Get(&metallic)) material.channels.emplace(types::MaterialChannel::Metallic, metallic);
            }
        }

        // 3. Roughness
        if (pxr::UsdShadeInput roughnessInput = shader.GetInput(pxr::TfToken("roughness")))
        {
            // diffuse color could be a single value or a referenced texture
            if (roughnessInput.HasConnectedSource())
            {
                material.channels.emplace(types::MaterialChannel::Roughness, GetConnectedTexture(roughnessInput));
            }
            else
            {
                class pxr::GfVec3f roughness;
                if (roughnessInput.Get(&roughness))
                    material.channels.emplace(types::MaterialChannel::Roughness, roughness);
            }
        }

        // 4. Normal
        if (pxr::UsdShadeInput normalInput = shader.GetInput(pxr::TfToken("normal")))
        {
            if (normalInput.HasConnectedSource())
            {
                material.channels.emplace(types::MaterialChannel::Normal, GetConnectedTexture(normalInput));
            }
        }

        // 4. Ambient Occlusion
        if (pxr::UsdShadeInput occlusionInput = shader.GetInput(pxr::TfToken("occlusion")))
        {
            if (occlusionInput.HasConnectedSource())
            {
                material.channels.emplace(types::MaterialChannel::AmbientOcclusion,
                                          GetConnectedTexture(occlusionInput));
            }
        }

        // 5. Opacity
        if (pxr::UsdShadeInput thresholdInput = shader.GetInput(pxr::TfToken("opacityThreshold")))
        {
            // the opacity threshold defines at which alpha value in the diffuse texture the texture shall
            // be treated as fully transparent (masked)
            float threshold = 0.0;
            if (!thresholdInput.Get(&material.opacityThreshold))
            {
                material.opacityThreshold = 0.0f;
            }
        }
        if (pxr::UsdShadeInput opacityInput = shader.GetInput(pxr::TfToken("opacity")))
        {
            if (opacityInput.HasConnectedSource())
            {
                material.channels.emplace(types::MaterialChannel::Opacity, GetConnectedTexture(opacityInput));
            }
            else
            {
                float opacity;
                if (opacityInput.Get(&opacity)) material.channels.emplace(types::MaterialChannel::Opacity, opacity);
            }
        }

        // 6. Emissive
        if (pxr::UsdShadeInput emissiveInput = shader.GetInput(pxr::TfToken("emissiveColor")))
        {
            if (emissiveInput.HasConnectedSource())
            {
                material.channels.emplace(types::MaterialChannel::Emissive, GetConnectedTexture(emissiveInput));
            }
            else
            {
                class pxr::GfVec3f emissive;
                if (emissiveInput.Get(&emissive)) material.channels.emplace(types::MaterialChannel::Emissive, emissive);
            }
        }

        // 7. Specular
        if (pxr::UsdShadeInput specularInput = shader.GetInput(pxr::TfToken("specular")))
        {
            // Godot does not provide a specular texture channel as of now, only fixed color is supported.
            float specular;
            if (specularInput.Get(&specular)) material.channels.emplace(types::MaterialChannel::Specular, specular);
        }
        // Specular from IOR
        if (pxr::UsdShadeInput iorInput = shader.GetInput(pxr::TfToken("ior")))
        {
            float ior = 1.5;
            if (iorInput.Get(&ior))
            {
                // the USD IOR value specifies the Godot specular factor
                float f = (ior - 1) / (ior + 1);
                float specular = std::sqrt(f * f / 0.08f);
                material.channels.emplace(types::MaterialChannel::Specular, std::clamp(specular, 0.0f, 1.0f));
                // a very high IOR value most likely indicates the surface is super reflective and thus acting close to
                // a mirror. Thus set the metallic value in the material to 1.0
                if (ior > 5.0) material.channels.emplace(types::MaterialChannel::Specular, 1.0f);
            }
        }

        return material;
    }

  protected:
    std::optional<types::TextureDescription<TextureType>> GetConnectedTexture(const pxr::UsdShadeInput& Input)
    {
        // get the connected source data
        pxr::UsdShadeConnectableAPI source = Input.GetConnectedSources()[0].source;
        class pxr::TfToken sourceChannel = Input.GetConnectedSources()[0].sourceName;

        // extract the file property
        pxr::UsdShadeInput file = source.GetInput(pxr::TfToken("file"));
        if (!file) return {};

        // get the actual filename
        pxr::SdfAssetPath filePath;
        if (!file.Get(&filePath)) return {};

        types::TextureDescription<TextureType> texture;

        texture.channel = sourceChannel;

        // use the openUSD AssetResolver to load the data from the file. This will ensure that
        // references into usdz files are handled properly, as well as remote resources, in case the file
        // is hosted on a remote location.
        // We might consider loading the texture data at another location to ensure we can provide a way to prevent
        // loading the same texture file multiple times into memory.
        // store the asset path into the texture description for convinience or debugging
        texture.filePath = filePath;

        // extract additional properties of the texture
        // this scale and bias values are usually only relevent for normal mapping textures
        // to turn the color coded [0;1] range into a [-1; 1] range. This is done automatically
        // in game engines, once a texture of type "normal map" is passed to the material shader.
        // however, scale might be used as base color tint in diffuse textures, thus extract it anyway
        if (pxr::UsdShadeInput scaleInput = source.GetInput(pxr::TfToken("scale"))) scaleInput.Get(&texture.scale);
        // pxr::UsdShadeInput biasInput = source.GetInput(pxr::TfToken("bias"));
        if (pxr::UsdShadeInput wrapUInput = source.GetInput(pxr::TfToken("wrapS")))
            texture.uWrapping = convertWrapMode(wrapUInput);
        if (pxr::UsdShadeInput wrapVInput = source.GetInput(pxr::TfToken("wrapT")))
            texture.vWrapping = convertWrapMode(wrapVInput);

        if (pxr::UsdShadeInput stInput = source.GetInput(pxr::TfToken("st")))
        {
            if (stInput.HasConnectedSource())
            {
                pxr::UsdShadeConnectableAPI& stSource = stInput.GetConnectedSources()[0].source;
                if (pxr::UsdShadeInput varname_in = stSource.GetInput(pxr::TfToken("varname")))
                {
                    std::string varname;
                    // this is usually the hint of the uv/st PrimVar name of the mesh, this material
                    // is assigned to, the uv coordinates are extracted from.
                    if (!varname_in.Get(&texture.stPrimVarName))
                    {
                        texture.stPrimVarName = "st";
                    }
                }
                if (pxr::UsdShadeInput scaleInput = stSource.GetInput(pxr::TfToken("scale")))
                {
                    if (!scaleInput.Get(&texture.uvScale))
                    {
                        texture.uvScale = pxr::GfVec2f(1, 1);
                    }
                }

                if (pxr::UsdShadeInput translationInput = stSource.GetInput(pxr::TfToken("translation")))
                {
                    if (!translationInput.Get(&texture.uvOffset))
                    {
                        texture.uvOffset = pxr::GfVec2f(0, 0);
                    }
                }

                if (pxr::UsdShadeInput rotationInput = stSource.GetInput(pxr::TfToken("rotation")))
                {
                    if (!rotationInput.Get(&texture.uvRotation))
                    {
                        texture.uvRotation = 0.0f;
                    }
                }
            }
        }
        return texture;
    }

    types::TextureWrapMode convertWrapMode(const pxr::UsdShadeInput& usdWrapModeInput)
    {
        class pxr::TfToken usdWrapMode;
        if (usdWrapModeInput && usdWrapModeInput.Get(&usdWrapMode))
        {
            if (usdWrapMode == pxr::TfToken("repeat"))
            {
                return types::TextureWrapMode::Repeat;
            }
            else if (usdWrapMode == pxr::TfToken("clamp"))
            {
                return types::TextureWrapMode::Clamp;
            }
            else if (usdWrapMode == pxr::TfToken("mirror"))
            {
                return types::TextureWrapMode::Mirror;
            }
        }

        return types::TextureWrapMode::Repeat;
    }
};
} // namespace converter
} // namespace idtxflow