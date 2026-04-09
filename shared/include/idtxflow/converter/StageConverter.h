#pragma once

/**
 * @file StageConverter.h
 * @brief Converter for an entire UsdStage into a game engine specific entity.
 * 
 * This is the main entry point for the conversion of a USD stage and is responsible to coordinate the conversion of all contained prims and assets into game engine specific
 * entities and data structures. The actual conversion of the individual prims and assets of the most common prim types is handled in this calss, while the engine specific
 * conversion logic needs to be specialized within target engine specific code.
 *  
 */
#include <filesystem>
#include <map>
#include <functional>
#include <random>
#include <array>
#include <string>

#include <idtxflow/utils/Logger.h>

#include <pxr/base/tf/token.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdUtils/flattenLayerStack.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/pcp/layerStack.h>
#include <pxr/usd/pcp/layerStackIdentifier.h>

#include "../types/TargetTypes.h"
#include "../cache/ResourceCache.h"
#include "TypeConverter.h"
#include "MeshConverter.h"
#include "MaterialConverter.h"
#include "AnimationConverter.h"
#include "PrimConverterRegistry.h"
#include "SkeletonConverter.h"

namespace idtxflow
{
namespace helper
{
    static inline std::string uuid_v4()
    {
        std::array<unsigned char, 16> b{};

        // Best-effort entropy without external libraries
        std::random_device rd;
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& x : b) x = static_cast<unsigned char>(dist(rd));

        // Set RFC 4122 version and variant
        b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40); // version 4
        b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80); // variant 1 (10xxxxxx)

        // Format as 8-4-4-4-12 hex string
        static const char* hex = "0123456789abcdef";
        std::string s;
        s.reserve(36);
        for (int i = 0; i < 16; ++i) {
            s.push_back(hex[b[i] >> 4]);
            s.push_back(hex[b[i] & 0x0F]);
            if (i == 3 || i == 5 || i == 7 || i == 9) s.push_back('-');
        }
        return s;
    }    
}
    
namespace converter
{
    template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
    class UsdStageConverter
    {
        IDTX_LOG_CATEGORY("StageConverter")
        
    public:
        using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
        using TypeConverter = UsdTypeConverter<TargetEngine>;

        /**
         * Instantiate the UsdStageConverter
         * @param inOwningEntity The game engine specific entity owning the entities converted from USD
         * @param inResourceCache The resource cache to be used to cache textures and materials for re-use during conversion
         */
        UsdStageConverter(typename Types::OwningEntity* inOwningEntity = nullptr, cache::UsdResourceCache<TargetEngine>* inResourceCache = nullptr)
        {
        	OwningEntity = inOwningEntity;
            ResourceCache = inResourceCache;
        };

        /**
         * Convert the usdStage into a game engine specific entity.
         * @param stage The stage to be converted
         * @param rootEntity If the converted entity shall be addedd to this root entity after convertion.
         * @return The returned list contains either the root entity holding all converted prims in the same tree structure
         *         as the USD stage did, the converted stages root prim or a list of prims that belonged to the pseudo-root
         *         of the usd stage.
         *         It is assumed that the target engines entity type allows to maintain a hierarchy similar to the one
         *         provided by USD. To actually create this game engine specific relation ship the `ConvertPrimPostProcess`
         *         method has to be specialized to implement this.
         */
        std::vector<typename Types::ConvertedEntity*> Convert(const pxr::UsdStageRefPtr& stage, typename Types::ConvertedEntity* rootEntity = nullptr)
        {
            Stage = stage;
            StageUpAxis = pxr::UsdGeomGetStageUpAxis(stage);
            StageMetersPerUnit = pxr::UsdGeomGetStageMetersPerUnit(stage);
            if (StageMetersPerUnit == 0.0) StageMetersPerUnit = 0.01;
            StageTimecodesPerSec = stage->GetTimeCodesPerSecond();
            if (StageTimecodesPerSec == 0) StageTimecodesPerSec = 24.0;
            StageAnimationStart = stage->GetStartTimeCode() / StageTimecodesPerSec;
            StageAnimationEnd = stage->GetEndTimeCode() / StageTimecodesPerSec;
            StageAnimationLength = StageAnimationEnd - StageAnimationStart;

            std::vector<typename Types::ConvertedEntity*> convertedEntities = ConvertPrims(stage, stage->TraverseAll(), rootEntity);
            StagePrototypeMap.clear();
            
            return ConvertStagePostProcess(convertedEntities);
        }
        
    protected:
        /**
         * Converts the PrimRange of a specific USD Stage.
         * @param stage The stage to be converted
         * @param primRange The range of prims within the stage that shall be converted.
         * @param rootEntity If the converted entity shall be addedd to this root entity after convertion.
         * @return The returned list contains either the root entity holding all converted prims in the same tree structure
         *         as the USD stage did, the converted stages root prim or a list of prims that belonged to the pseudo-root
         *         of the usd stage.
         *         It is assumed that the target engines entity type allows to maintain a hierarchy similar to the one
         *         provided by USD. To actually create this game engine specific relation ship the `ConvertPrimPostProcess`
         *         method has to be specialized to implement this.
         */
        std::vector<typename Types::ConvertedEntity*> ConvertPrims(const pxr::UsdStageRefPtr& stage, const pxr::UsdPrimRange& primRange, typename Types::ConvertedEntity* rootEntity = nullptr)
        {
            // the traversal of prims in the UsdPrimRange happens depth first. This means that the parent prims
            // are always visited before their childs. To be able to reconstruct this hierarchy while converting the
            // Prims we need to maintain a mapping between the converted prim and the original to retrieve the correct parent
            // the mapping stores the original prims path within the stage and a reference/pointer to the converted version
            std::map<pxr::SdfPath, typename Types::ConvertedEntity*> primToConvertedMap;

            // this lamda looks up the converted parent of a given Prim. As some Prims are only "structural" like 'UsdScope',
            // the direct parent might not be converted. Thus we recursively looking up the parents until we have found one
            std::function<typename Types::ConvertedEntity* (const pxr::UsdPrim&)> getConvertedParent = [&](const pxr::UsdPrim& prim) -> typename Types::ConvertedEntity*
            {
                pxr::UsdPrim parentPrim = prim.GetParent();
                // if this is the pseudo root or invalid we actually are unable to further walk up the usd prim tree
                if (!parentPrim.IsValid() || parentPrim.IsPseudoRoot()) return nullptr;
                pxr::SdfPath parentPrimPath = parentPrim.GetPath();
                if (primToConvertedMap.contains(parentPrimPath))
                {
                    return primToConvertedMap.at(parentPrimPath);
                }

                return getConvertedParent(parentPrim);
            };

            // iterating over the primRange will visit the "root" prim first. This could be:
            // - the pseudo root, if the primRange is created for a stage and this stage does not author a default Prim
            // - the default prim, if the primRange is created for a stage that has a default prim authored
            // - the pim that has created the primRange to access all its child prims.
            
            std::vector<typename Types::ConvertedEntity*> convertedEntities;
            
            // set the root for all prims to be converted. This allows to put all prims underneath a single node, that
            // is provided by the caller. Note that the parent-child-relationship has to be done in the implementation of
            // ConvertPrimPostProcess
            typename Types::ConvertedEntity* convertedRootPrim = rootEntity;
            if (rootEntity != nullptr) convertedEntities.push_back(rootEntity);
            
            for (auto it = primRange.begin(); it != primRange.end(); ++it)
            {
                const pxr::UsdPrim& usdPrim = *it;
                
                // we will not convert inactive, abstract or undefined prims - and also no real prototype of real instances
                if (!usdPrim.IsActive() || usdPrim.IsAbstract() || usdPrim.IsPrototype() || !usdPrim.IsDefined())
                {
                    // skip this and skip all decendents
                    it.PruneChildren();
                    continue;
                }
                
                bool shouldPruneChildren = false;
                if (typename Types::ConvertedEntity* convertedPrim = ConvertPrim(usdPrim, shouldPruneChildren))
                {
                    // get the parent of the converted node
                    typename Types::ConvertedEntity* convertedParent = getConvertedParent(usdPrim);
                    if (convertedParent == nullptr)
                    {
                        // if not available choose the root as parent
                        convertedParent = rootEntity;
                    }
                    
                    // this method shall be specialized for each GameEngine if individual post processing is required
                    // this includes maintaining the parent-child relationship of converted nodes
                    convertedPrim = ConvertPrimPostProcess(usdPrim, convertedPrim, convertedParent);
                    
                    // Delegate to the PrimConverterRegistry: if a third-party plugin has registered a converter
                    // for this prim's type name, use it's PostProcess after the default Postprocessing
                    pxr::TfToken primTypeName = usdPrim.GetTypeName();
                    auto& registry = PrimConverterRegistry<TargetEngine>::Instance();
                    if (IPrimConverter<TargetEngine>* converter = registry.Get(primTypeName))
                    {
                        convertedPrim = converter->PostProcess(usdPrim, convertedPrim, convertedParent);
                    } 
                    
                    // if the converted Prim does not have any parent it is added to the list of converted prims
                    // this ensures the list only contains the root prim or all converted prims that has been authored
                    // beneath the pseudoroot of a stage
                    if (convertedParent == nullptr)
                    {
                        convertedEntities.push_back(convertedPrim);
                    }
                    
                    primToConvertedMap.emplace(usdPrim.GetPath(), convertedPrim);
                    
                    if (shouldPruneChildren) it.PruneChildren();
                }
            }
            
            return convertedEntities;
        }

        /**
         * Convert a single Prim from USD into the game engine corresponding entity type
         * @param usdPrim The prim to be converted
         * @param outPruneChildren Set this to true to skip all childrens from processing after this one
         * @return The target engine specific entity representing this prim
         */
        typename Types::ConvertedEntity* ConvertPrim(const pxr::UsdPrim& usdPrim, bool& outPruneChildren)
        {
            outPruneChildren = false;
            typename Types::ConvertedEntity* convertedEntity = nullptr;
            // in case the stage has been loaded with LoadNone, the prims that refer to a payload are not fully available
            // for conversion. Thus we branch here to handle prims with a postponed load
            if (usdPrim.HasAuthoredPayloads() && !usdPrim.IsLoaded())
            {
                // Target Engines might handle the case of a postponed payload prim differently. Thus we perpare some
                // data that might be relevant from the prim already and then let the engine spezialized functionality
                // do the "heavy" work.
                // However, the posponed payload handling is required to handle all it's children on its owm. Thus
                // skipping them in the current conversion. If the current stage, referring to a payload authored
                // childrens this is passed to the payload stage as override layer to allow the postponed conversion
                // to add this layer on top of the stage to provide the stronger opinion as intended
                outPruneChildren = true;
                // we will explore the layerstack of this prim to find the strongest opinion on the payload for this
                // prim. This gives us the origin of the payload (uri) that would be required to load to access the 
                // contents of this prim
                std::string payloadUri;
                pxr::SdfPath payloadPrimPath;
                
                const pxr::PcpPrimIndex& primIndex = usdPrim.GetPrimIndex();
                for (const pxr::PcpNodeRef& node: primIndex.GetNodeRange())
                {
                    const pxr::PcpLayerStackRefPtr& layerStack = node.GetLayerStack();
                    for (const pxr::SdfLayerRefPtr& layer : layerStack->GetLayers())
                    {
                        const pxr::SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(node.GetPath());
                        if (!primSpec) continue;
        
                        for (const pxr::SdfPayload& payload : primSpec->GetPayloadList().GetAppliedItems()) {
                            payloadUri = pxr::ArGetResolver().CreateIdentifier(payload.GetAssetPath(), layer->GetResolvedPath());
                            payloadPrimPath = payload.GetPrimPath();
                            break;
                        }
                        
                        if (!payloadUri.empty()) break;
                    }

                    if (!payloadUri.empty()) break;
                }
                
                // in case the prim is a xformable, we extract the transform matrix for it. Otherwise we use the default
                // identity matrix as transform
                class pxr::GfMatrix4d transform;
                if (usdPrim.IsA<pxr::UsdGeomXform>())
                {
                    pxr::UsdGeomXformable xformable(usdPrim);
                    bool resets;
                    if (!xformable.GetLocalTransformation(&transform, &resets))
                        transform.SetIdentity();
                } else
                {
                    transform.SetIdentity();
                }
                
                // when converting the payload prim as seperate usd stage from it's uri we will loose the authored properties
                // or overrides in this very prim spec. Thus we will extract this prim spec into an anonymous layer and pass
                // this one to the conversation method as well to be applied when loading the stage. This would compose the
                // layer data into the stage and thus create the expected state
                pxr::SdfLayerRefPtr overrideLayer = pxr::SdfLayer::CreateAnonymous("override_layer");
                
                pxr::SdfPath targetPath;
                // if the payload reference contained a prim path we can copy the prim spec for this path into the 
                // override layer straight away. If this is not the case we need to get the default prim of the
                // referenced layer, without loading/composing the whole stage
                if (payloadPrimPath.IsEmpty())
                {
                    pxr::SdfLayerRefPtr payloadLayer = pxr::SdfLayer::FindOrOpen(payloadUri);
                    if (payloadLayer)
                    {
                        if (payloadLayer->HasDefaultPrim())
                            targetPath = pxr::SdfPath::AbsoluteRootPath().AppendChild(
                                payloadLayer->GetDefaultPrim());
                        else
                            targetPath = pxr::SdfPath::AbsoluteRootPath();
                    }
                } else
                {
                    targetPath = payloadPrimPath;
                }
                // copy the prim spec into the layer (either from the current session layer, that would be the
                // override layer for a nested paylod prim, or the stages root layer. If the prim exists in both
                // we need to compose them as the sesion layer may provide a stronger opinion to things in the
                // root layer that would get lost when only grabbing the primSpec from the session layer
                pxr::SdfLayerHandle sessionLayer = usdPrim.GetStage()->GetSessionLayer();
                pxr::SdfPrimSpecHandle sessionPrimSpec = sessionLayer->GetPrimAtPath(usdPrim.GetPath());
                pxr::SdfLayerHandle rootLayer = usdPrim.GetStage()->GetRootLayer();
                pxr::SdfPrimSpecHandle rootPrimSpec = rootLayer->GetPrimAtPath(usdPrim.GetPath());
                if (sessionPrimSpec && rootPrimSpec)
                {
                    // we need to compose both layers to pass the composed prim spec as override layer to
                    // the postponed payload handling to provide a complete picture there
                    pxr::UsdStageRefPtr composerStage = pxr::UsdStage::CreateInMemory();
                    composerStage->GetRootLayer()->GetSubLayerPaths().push_back(rootLayer->GetIdentifier());
                    composerStage->GetRootLayer()->GetSubLayerPaths().push_back(sessionLayer->GetIdentifier());
                    pxr::SdfLayerRefPtr composedLayer = pxr::UsdUtilsFlattenLayerStack(composerStage);
                    
                    std::string content;
                    composerStage->GetRootLayer()->ExportToString(&content);
                    composedLayer->ExportToString(&content);
                    
                    // copy the composed version into the override layer
                    pxr::SdfCopySpec(
                        composedLayer, //composerStage->GetRootLayer(),
                        usdPrim.GetPath(),
                        overrideLayer,
                        targetPath
                    );
                } else if (sessionPrimSpec)
                {
                    // there is only the session spec for this prim - copy it
                    pxr::SdfCopySpec(
                        usdPrim.GetStage()->GetSessionLayer(),
                        usdPrim.GetPath(),
                        overrideLayer,
                        targetPath
                    );
                } else
                {
                    // there is only the root spec for this prim - copy it
                    pxr::SdfCopySpec(
                        usdPrim.GetStage()->GetRootLayer(),
                        usdPrim.GetPath(),
                        overrideLayer,
                        targetPath
                    );
                }
                
                // in the target layer we need to remove the payload and we need to adjust any path references
                // to anchor at the stage that is referenced as payload, as the authored paths are ancored around the
                // current stage
                if (pxr::SdfPrimSpecHandle overridePrim = overrideLayer->GetPrimAtPath(targetPath))
                {
                    // clean out the payload for the prim to eliminate recursion
                    overridePrim->ClearPayloadList();
                } else
                {
                    IDTX_LOG(IDTX_WARN, "Prim not found in override layer after copy spec from {} to {}",
                        usdPrim.GetPath().GetText(), targetPath.GetText());
                }
                
                // we need to create an identifier for the layer with the path to the stage the layer shall provide a stronger
                // opinion for. Only then relative resource paths will be properly resolved. However, the identifier is required
                // to be unique when the same referred payload is used. Thus extract the unique part of the anonymous id the
                // layer is created with and use it as "filename" part of the layer identifyer.
                std::string layerPath = std::filesystem::path(usdPrim.GetStage()->GetRootLayer()->GetRealPath())
                    .parent_path()
                    .string();
                std::string lp;
                pxr::SdfLayer::FileFormatArguments args;
                pxr::SdfLayer::SplitIdentifier(overrideLayer->GetIdentifier(), &lp, &args);
                auto first = lp.find(":");
                auto second = lp.find(":", first + 1);
                std::string layer_identifier = pxr::SdfLayer::CreateIdentifier(
                    layerPath + "/" + helper::uuid_v4() +  ".usda",
                    pxr::SdfLayer::FileFormatArguments{});
                overrideLayer->SetIdentifier(layer_identifier);
                
                return ConvertPrimWithPayload(usdPrim, payloadUri, TypeConverter::toTransform(transform), overrideLayer);
            }
            
            // for any prim type that is not a payload one, check if there is an extension/plugin registered to handle it
            // this includes custom prim types not having a built-in conversion logic
            pxr::TfToken primTypeName = usdPrim.GetTypeName();
            auto& registry = PrimConverterRegistry<TargetEngine>::Instance();

            // find the highest priority converter
            if (IPrimConverter<TargetEngine>* converter = registry.Get(primTypeName))
            {
                return converter->Convert(usdPrim);
            }
            
            // continue with built-in conversion for the known prim typs
            if (usdPrim.IsA<pxr::UsdShadeMaterial>())
            {
                pxr::UsdShadeMaterial usdMaterial(usdPrim);
                ConvertMaterial(usdMaterial);
                // a material conversion does not create an entity but adds the converted material to the ResourceCache
                convertedEntity = nullptr;
            } else if (usdPrim.IsA<pxr::UsdGeomXform>())
            {
                // convert a simple transform prim that does only author transform info but no visual apperance
                pxr::UsdGeomXform usdXform(usdPrim);
                class pxr::GfMatrix4d matrix;
                bool resets;
                if (!usdXform.GetLocalTransformation(&matrix, &resets)) matrix.SetIdentity();
                
                UsdAnimationConverter<TargetEngine> animationConverter;
                std::optional<AnimationDescription<TargetEngine>> xFormAnimation = animationConverter.Convert(usdXform, StageTimecodesPerSec);

                convertedEntity = ConvertXform(TypeConverter::toTransform(matrix), xFormAnimation);
            } else if (usdPrim.IsA<pxr::UsdGeomGprim>() && !pxr::UsdSkelRoot::Find(usdPrim))
            {
                pxr::UsdGeomGprim usdGprim(usdPrim);
                // convert a geometric prim that is not skinning a skeleton.
                // This one has transform and visual appearance, either as primitive or as
                // complex mesh
                // Gprims might be authored in a usdStage as "Pseudoinstances". Treating them as individual prims might
                // increase memory consumption and reduce performance dramatically. Thus we will convert those into
                // actual MeshInstances
                pxr::SdfPath usdPseudioInstancePrototypePath;
                if (IsPseudoinstance(usdPrim, &usdPseudioInstancePrototypePath))
                {
                    class pxr::GfMatrix4d matrix;
                    bool resets;
                    if (!usdGprim.GetLocalTransformation(&matrix, &resets)) matrix.SetIdentity();
                    
                    convertedEntity = ConvertGprimPseudoInstance(TypeConverter::toTransform(matrix), usdGprim, usdPseudioInstancePrototypePath);
                } else
                {
                    convertedEntity = ConvertGprim(usdGprim);
                }
            } else if (usdPrim.IsA<pxr::UsdSkelRoot>())
            {
                // the SkelRoot prim is the root prim for all skeletons. Thus any skeleton prim has to be converted as
                // a child of the SkelRoot. An USD with a skeleton without a SkelRoot as parent is malformed.
                // Let's convert all skeletons below this SkelRoot. The general and most likely scenario is, that there
                // will be only one Skeleton below the SkelRoot. Exeptions might be crowds and the like, where multiple
                // skeletons will be placed below one SkelRoot.
                pxr::UsdSkelRoot usdSkelRoot(usdPrim);
                for (pxr::UsdPrim child: pxr::UsdPrimRange(usdPrim))
                {
                    if (child.IsA<pxr::UsdSkelSkeleton>())
                    {
                        pxr::UsdSkelSkeleton usdSkelSkeleton(child);
                        
                        class pxr::GfMatrix4d matrix;
                        bool resets;
                        if (!usdSkelSkeleton.GetLocalTransformation(&matrix, &resets)) matrix.SetIdentity();
                        
                        UsdSkeletonConverter<TargetEngine> skeletonConverter;
                        SkeletonDescription<TargetEngine> skeletonDescription = skeletonConverter.Convert(usdSkelRoot, usdSkelSkeleton);
                        
                        UsdAnimationConverter<TargetEngine> animationConverter;
                        std::optional<AnimationDescription<TargetEngine>> skeletonAnimation = animationConverter.Convert(usdSkelRoot, usdSkelSkeleton, StageTimecodesPerSec);
                        
                        convertedEntity = ConvertSkeleton(TypeConverter::toTransform(matrix), skeletonAnimation, skeletonDescription);
                        
                        // as mentioned, we will only convert one skeleton of the UsdSkelRoot for the time being
                        break;
                    }
                }
            }
            
            return convertedEntity;
        }
        
    public:
        std::optional<typename Types::Material> ConvertMaterial(const pxr::UsdShadeMaterial& usdMaterial)
        {
            UsdMaterialConverter<TargetEngine> materialConverter;
            std::optional<types::MaterialDescription<typename UsdMaterialConverter<TargetEngine>::TextureType>> materialDescription = materialConverter.Convert(usdMaterial);
            std::optional<typename Types::Material> material = std::nullopt;
            
            if (materialDescription.has_value())
            {
                const auto& materialDesc = materialDescription.value();
                // check if we have a cache and if so, if we have cached the material already.
                if (ResourceCache && ResourceCache->HasCachedMaterial(materialDesc.id))
                {
                    material = ResourceCache->GetCachedMaterial(materialDesc.id);
                } else
                {
                    material = TypeConverter::toMaterial(materialDesc, Stage);
                    if (ResourceCache && material.has_value())
                    {
                        ResourceCache->CacheMaterial(materialDesc.id, material.value());
                    }
                }
            }
            
            return material;
        }
    protected:
        /**
         * Convert a Prim that has an authored payload and is not yet been loaded by the openUSD library and this not
         * composed into the stage.
         * @param usdPrim The Prim to be converted
         * @param payloadUri The extracted payload source URI/path the prim could be loaded from.
         * @param transform The transform of the prim if it is a xformable or the identity transform
         * @return 
         */
        typename Types::ConvertedEntity* ConvertPrimWithPayload(
            const pxr::UsdPrim& usdPrim,
            const std::string& payloadUri,
            const typename Types::Transform& transform,
            const pxr::SdfLayerRefPtr& overrideLayer);

        /**
         * Convert a Xform Prim
         * @param transform The transform already converted into the game engine specific type
         * @param animation The animation data for this transform, if any
         * @return The game engine specific entity representing this prim
         */
        typename Types::ConvertedEntity* ConvertXform(
            const typename Types::Transform& transform,
            const std::optional<AnimationDescription<TargetEngine>>& animation);

        /**
         * Convert Gemoetric prim that holds data for visual representation
         * @param usdGprim The Geom Prim that shall be converted
         * @return The game engine specific entity representing this prim
         */
        typename Types::ConvertedEntity* ConvertGprim(const pxr::UsdGeomGprim& usdGprim)
        {
            class pxr::GfMatrix4d matrix;
            bool resets;
            if (!usdGprim.GetLocalTransformation(&matrix, &resets)) matrix.SetIdentity();
            
            pxr::VtArray<class pxr::GfVec3f> colors;
            usdGprim.GetDisplayColorAttr().Get(&colors);
            pxr::VtArray<float> opacities;
            usdGprim.GetDisplayOpacityAttr().Get(&opacities);
            
            // color and opacity are authored in different properties to enable independent overwrites, however we pass
            // this as RGBA color to downstream functions. Thus, we need to combine them into one rgba color
            const size_t colorCount = std::max(colors.size(), opacities.size());
            pxr::VtArray<class pxr::GfVec4f> displayColors(colorCount);
            for (size_t i = 0; i < colorCount; ++i) {
                const pxr::GfVec3f& rgb = i < colors.size() ? colors[i] : pxr::GfVec3f(1.0f);
                const float a = i < opacities.size() ? opacities[i] : 1.0f;
                displayColors[i] = pxr::GfVec4f(rgb[0], rgb[1], rgb[2], a);
            }
            
            class pxr::TfToken colorInterpolation = usdGprim.GetDisplayColorPrimvar().GetInterpolation();
            
            UsdAnimationConverter<TargetEngine> animationConverter;
            std::optional<AnimationDescription<TargetEngine>> gPrimAnimation = animationConverter.Convert(usdGprim, StageTimecodesPerSec);

            UsdMaterialConverter<TargetEngine> materialConverter;
            UsdMeshConverter<TargetEngine> meshConverter;
            std::optional<typename Types::Material> material = ConvertMaterial(meshConverter.GetUsdMaterial(usdGprim));
            pxr::UsdPrim usdPrim = usdGprim.GetPrim();
            if (usdPrim.IsA<pxr::UsdGeomCube>())
            {
                pxr::UsdGeomCube usdCube(usdPrim);
                double cubeSize;
                if (!usdCube.GetSizeAttr().Get(&cubeSize)) cubeSize = 1.0;
                
                return ConvertCube(TypeConverter::toTransform(matrix), gPrimAnimation, material, cubeSize, displayColors, colorInterpolation);

            } else if (usdPrim.IsA<pxr::UsdGeomCone>())
            {

                pxr::UsdGeomCone usdCone(usdPrim);
                class pxr::TfToken axis;
                usdCone.GetAxisAttr().Get(&axis);
                double coneRadius;
                double coneHeight;
                if (!usdCone.GetHeightAttr().Get(&coneHeight)) coneHeight = 1.0;
                if (!usdCone.GetRadiusAttr().Get(&coneRadius)) coneRadius = 1.0;
                
                return ConvertCone(TypeConverter::toTransform(matrix, axis), gPrimAnimation, material, coneRadius, coneHeight, displayColors, colorInterpolation);

            } else if (usdPrim.IsA<pxr::UsdGeomCylinder>())
            {
                pxr::UsdGeomCylinder usdCylinder(usdPrim);
                double cylinderRadius;
                double cylinderHeight;
                if (!usdCylinder.GetRadiusAttr().Get(&cylinderRadius)) cylinderRadius = 1.0;
                if (!usdCylinder.GetHeightAttr().Get(&cylinderHeight)) cylinderHeight = 1.0;
                
                class pxr::TfToken axis;
                usdCylinder.GetAxisAttr().Get(&axis);
                    
                return ConvertCylinder(TypeConverter::toTransform(matrix, axis), gPrimAnimation, material, cylinderRadius, cylinderHeight, displayColors, colorInterpolation);

            } else if (usdPrim.IsA<pxr::UsdGeomSphere>())
            {
                pxr::UsdGeomSphere usdSphere(usdPrim);
                double sphereRadius;
                if (!usdSphere.GetRadiusAttr().Get(&sphereRadius)) sphereRadius = 0.5;
                return ConvertSphere(TypeConverter::toTransform(matrix), gPrimAnimation, material, sphereRadius, displayColors, colorInterpolation);

            } else if (usdPrim.IsA<pxr::UsdGeomMesh>())
            {
                pxr::UsdGeomMesh usdMesh(usdPrim);
                std::vector<MeshDescription<typename UsdMeshConverter<TargetEngine>::MeshDataType>> meshDescriptions = meshConverter.Convert(usdMesh);
                                
                return ConvertMesh(TypeConverter::toTransform(matrix), gPrimAnimation, meshDescriptions, displayColors, colorInterpolation);
            }
            
            return nullptr;
        }

        /**
         * Convert a GeomPrim that references a kind of prototype in the USD stage without using actual instancing, while
         * this prim and any "copy" of it, appearing in the USD stage can be treated as instances.
         * @param transform The transform of the instance
         * @param usdGprim The actual GeomPrim holding the visual reprentation for the instance
         * @param usdPrototypePath The path the GeomPrim that can be treated as Prototype for the instance. The actual
         * mesh data, material etc. is taken from here to create the game engine specific prototype.
         * @return 
         */
        typename Types::ConvertedEntity* ConvertGprimPseudoInstance(
            const typename Types::Transform& transform,
            const pxr::UsdGeomGprim& usdGprim,
            const pxr::SdfPath& usdPrototypePath);

        /**
         * Convert a Cube
         * @param transform The transform of the cube
         * @param animation The animation of the cubes transform if any
         * @param material  The material of the cube
         * @param cubeSize The size of the cube in usd units
         * @param displayColors The authored display colors
         * @param colorInterpolation The authored color interpolation
         * @return 
         */
        typename Types::ConvertedEntity* ConvertCube(
            const typename Types::Transform& transform,
            const std::optional<AnimationDescription<TargetEngine>>& animation,
            const std::optional<typename Types::Material>& material,
            float cubeSize,
            const pxr::VtArray<class pxr::GfVec4f>& displayColors,
            const class pxr::TfToken& colorInterpolation);

        /**
         * Convert a Cylinder
         * @param transform The transform of the cylinder. This incorporates the adjustments of the spline axis authored
         *                  in USD already, thus the cylinder grows already into the right direction.
         * @param animation The animation of the cylinders transform if any
         * @param material The material of the cylinder
         * @param cylinderRadius The radius
         * @param cylinderHeight The height
         * @param displayColors The authored display colors
         * @param colorInterpolation The authored color interpolation
         * @return 
         */
        typename Types::ConvertedEntity* ConvertCylinder(
            const typename Types::Transform& transform,
            const std::optional<AnimationDescription<TargetEngine>>& animation,
            const std::optional<typename Types::Material>& material,
            float cylinderRadius,
            float cylinderHeight,
            const pxr::VtArray<class pxr::GfVec4f>& displayColors,
            const class pxr::TfToken& colorInterpolation);

        /**
         * Convert a Cone
         * @param transform The transform of the cone. This incorporates the adjustments of the spline axis authored
         *                  in USD already, thus the cone grows already into the right direction.
         * @param animation The animation of the cone transform if any
         * @param material The material of the cone
         * @param coneRadius The radius
         * @param coneHeight The height
         * @param displayColors The authored display colors
         * @param colorInterpolation The authored color interpolation
         * @return 
         */
        typename Types::ConvertedEntity* ConvertCone(
            const typename Types::Transform& transform,
            const std::optional<AnimationDescription<TargetEngine>>& animation,
            const std::optional<typename Types::Material>& material,
            float coneRadius,
            float coneHeight,
            const pxr::VtArray<class pxr::GfVec4f>& displayColors,
            const class pxr::TfToken& colorInterpolation);

        /**
         * Convert a Sphere
         * @param transform The transform of the sphere
         * @param animation The animation of the spheres transform, if any
         * @param material The material of the sphere
         * @param sphereRadius The radius
         * @param displayColors The authored display colors
         * @param colorInterpolation The authored color interpolation
         * @return 
         */
        typename Types::ConvertedEntity* ConvertSphere(
            const typename Types::Transform& transform,
            const std::optional<AnimationDescription<TargetEngine>>& animation,
            const std::optional<typename Types::Material>& material,
            float sphereRadius,
            const pxr::VtArray<class pxr::GfVec4f>& displayColors,
            const class pxr::TfToken& colorInterpolation);

        /**
         * Convert a Mesh
         * @param transform The transform of the mesh
         * @param animation The animation of the meshs transform, if any
         * @param meshDescriptions The detailed description of the mesh, prepared for game engine specific implementation
         * @param displayColors The authored display colors
         * @param colorInterpolation The authored color interpolation
         * @return 
         */
        typename Types::ConvertedEntity* ConvertMesh(
            const typename Types::Transform& transform,
            const std::optional<AnimationDescription<TargetEngine>>& animation,
            const std::vector<MeshDescription<typename UsdMeshConverter<TargetEngine>::MeshDataType>>& meshDescriptions,
            const pxr::VtArray<class pxr::GfVec4f>& displayColors,
            const class pxr::TfToken& colorInterpolation);

        /**
         * Convert a Skeleton with it's skinning Mesh
         * @param transform The transform of the skeleton
         * @param animation The animation of the skeleton, if any
         * @param skeletonDescription The detailed skeleton data, prepared for the game engine specific implementation
         * @return 
         */
        typename Types::ConvertedEntity* ConvertSkeleton(
            const typename Types::Transform& transform,
            const std::optional<AnimationDescription<TargetEngine>>& animation,
            const SkeletonDescription<TargetEngine>& skeletonDescription);
        
        /**
         * Checks, if the actual prim that shall be converted can be treated as an instance of a prototype without actually
         * using the instancing feature of openUSD.
         * @param usdPrim The prim to be checked
         * @param prototypePath If the prim is a pseudeo instance, this will contain the path to the prototype in the stage
         * @return 
         */
        bool IsPseudoinstance(const pxr::UsdPrim& usdPrim, pxr::SdfPath* prototypePath)
        {
            // if it is an actual real instance, it's not a pseudo one
            if (usdPrim.IsInstanceable() || usdPrim.IsInstance()) return false;

            // if the prim has authored attributes that would make it impossible to be treated as an instance
            pxr::SdfPrimSpecHandle prim_spec = usdPrim.GetPrimStack().at(0);
            pxr::SdfAttributeSpecView spec_attributes = prim_spec.GetSpec().GetAttributes();
            if (spec_attributes.has(pxr::UsdGeomTokens->points) ||
                spec_attributes.has(pxr::UsdGeomTokens->normals) ||
                spec_attributes.has(pxr::UsdGeomTokens->faceVertexIndices) ||
                spec_attributes.has(pxr::UsdGeomTokens->faceVertexCounts))
                return false;
            
            // to analyze further we need the composition arcs paying into this one
            // from strongest to weakest
            pxr::UsdPrimCompositionQuery composition_query(usdPrim);
            for (const pxr::UsdPrimCompositionQueryArc& composition_arc: composition_query.GetCompositionArcs())
            {
                if (composition_arc.GetArcType() == pxr::PcpArcTypeReference ||
                    composition_arc.GetArcType() == pxr::PcpArcTypeInherit)
                {
                    *prototypePath = composition_arc.GetTargetPrimPath();
                    pxr::SdfLayerHandle prototype_layer = composition_arc.GetTargetLayer();
                    pxr::SdfLayerHandle stage_layer = usdPrim.GetStage()->GetRootLayer();

                    // if the referenced path is absolute and the prototype layer is the same as the prims layer
                    // - essentially being in the same file
                    if (prototypePath->IsAbsolutePath() && prototype_layer == stage_layer)
                    {
                        // next up, check if the prototype prim is defined as "over", thus it will usually be ignored from
                        // prim traversal and does not visualize itself. This identifies it even more as being a prototype
                        if (stage_layer->GetSpecType(*prototypePath) == pxr::SdfSpecTypePrim)
                        {
                            pxr::SdfPrimSpecHandle prototype_spec = stage_layer->GetPrimAtPath(*prototypePath);
                            if (prototype_spec && prototype_spec->GetSpecifier() == pxr::SdfSpecifierOver)
                            {
                                // when coming here, we can be quite certain, that the current prim is referencing
                                // a prototype that can be treated as source for pseudo-instancing
                                return true;
                            }
                            pxr::SdfPrimSpecHandle parent_spec = prototype_spec->GetNameParent();
                            if (parent_spec && parent_spec->GetSpecifier() == pxr::SdfSpecifierOver)
                            {
                                // in case prim spec in question is not defined as "over" check the immediate parent
                                return true;
                            }
                            // TODO: check if there might be more parents to be checked and if so - how many?
                            // the question would be: how many levels are usually autored for pseudo instances.
                        }
                    }
                    // only check the strongest composition layer reference. If a reference comes from weaker layers
                    // it's very unlikely, that we have a pseudo instancing scenario
                    break;
                }
            }

            return false;
        }

        /**
         * Postprocessing after the prim has been converted into game engine specific type
         * @param usdPrim The prim that has been converted
         * @param convertedPrim The entity the prim has been converted to
         * @param convertedParentPrim The parent entity that the parent prim of the current one has been converted to
         * @return 
         */
        typename Types::ConvertedEntity* ConvertPrimPostProcess(
            const pxr::UsdPrim& usdPrim,
            typename Types::ConvertedEntity* convertedPrim,
            typename Types::ConvertedEntity* convertedParentPrim);

        /**
         * Postprocessing after the whole stage has been converted into game engine specific types. As the converted
         * entities exists in the USD coordinate system, this is the best place to adjust the transform of the root entity
         * to ensure the converted scene is adjusted to the target engines coordinate system. This is more performant
         * copmared to adjust each converted prim individually or even convert the meshs vertices individually into the
         * target coordinate system.
         * @param convertedRootPrim The root entity of the conversion
         * @return 
         */
        std::vector<typename Types::ConvertedEntity*> ConvertStagePostProcess(
            const std::vector<typename Types::ConvertedEntity*>& convertedEntities);
        
    private:
    	// The entity that "owns" all converted prims/nodes
    	typename Types::OwningEntity* OwningEntity;
        // The authored UP axis of the stage to be converted
        class pxr::TfToken StageUpAxis;
        // reference to the stage that is being converted
        pxr::UsdStageRefPtr Stage;
        // The authored meters per unit in this stage. Default is 0.01. This means a mesh point at 1, 1, 1
        // would appear 1 centimeter from the center. If different layers of an USD stage authored different
        // values for this property, or if an asset references one with a diffrent metric there is no automatic
        // adjustment done in openUSD. Thus scaling is required to be applied.
        double StageMetersPerUnit;
        // The granularity of time codes used per second in this stages animations
        double StageTimecodesPerSec;
        // The animation start time (actual time in seconds)
        double StageAnimationStart;
        // The animation end time (actual time in seconds)
        double StageAnimationEnd;
        // The animation length in seconds
        double StageAnimationLength;
        // Store the converted prims that act as prototypes while converting pseudo-instance prims
        std::map<pxr::SdfPath, void*> StagePrototypeMap;
        // Store the resource cache that will be used during convertion of the stage
        cache::UsdResourceCache<TargetEngine>* ResourceCache;
    };
}
}
