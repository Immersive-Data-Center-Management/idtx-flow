#pragma once

#include <variant>
#include <optional>
#include <vector>
#include <string>

#include <pxr/base/tf/token.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/usd/sdf/assetPath.h>

namespace idtxflow
{
namespace types
{
enum class MaterialChannel
{
    Diffuse,
    Metallic,
    Roughness,
    Specular,
    Emissive,
    Opacity,
    Normal,
    AmbientOcclusion,
    ClearCoat,
    ORM
};

enum class TextureUsage
{
    BaseColor,
    Normal,
    Metallic,
    Roughness,
    Emissive,
    Opacity,
    AmbientOcclusion,
    ORM
};

enum class TextureWrapMode
{
    Repeat,
    Clamp,
    Mirror
};

template <typename Texture> struct TextureDescription
{
    pxr::SdfAssetPath filePath;              // asset Path to the texture file
    std::optional<Texture> texture;          // the converted texture
    std::optional<std::string> tileId;       // the teture tile id, if a tiled texture
    std::string stPrimVarName = "st";        // uv-coordinates prim var of the mesh prim
    class pxr::GfVec4f scale = {1, 1, 1, 1}; // generic texture value scaling
    class pxr::GfVec2f uvScale = {1, 1};     // uv coordinate scaling
    class pxr::GfVec2f uvOffset = {0, 0};    // uv coordinate offset
    float uvRotation = 0;                    // uv map rotation
    TextureWrapMode uWrapping = TextureWrapMode::Repeat;
    TextureWrapMode vWrapping = TextureWrapMode::Repeat;
    class pxr::TfToken channel = pxr::TfToken(); // the color channel to be used in the texture
};

template <typename Texture>
using MaterialChannelValue = std::variant<float,              // simple float value for this channel
                                          class pxr::GfVec3f, // Vector3 or Color w/o alpha value
                                          class pxr::GfVec4f, // Vector4 or Color value
                                          std::optional<TextureDescription<Texture>>, // Texture value
                                          std::vector<TextureDescription<Texture>> // if the material refers to multiple
                                                                                   // texture tiles we store them here
                                          >;

template <typename Texture> struct MaterialDescription
{
    std::string id; // matrial id - usually the path to the material within a composed stage
    std::map<MaterialChannel, MaterialChannelValue<Texture>> channels; // the different matrial channel values
    float opacityThreshold = 0.0f; // the opacity threshold when alpha channel in albedo texture is used as cut-out
    bool withTiles = false;        // when at least 1 channel uses texture tiles this is true
};
} // namespace types
} // namespace idtxflow