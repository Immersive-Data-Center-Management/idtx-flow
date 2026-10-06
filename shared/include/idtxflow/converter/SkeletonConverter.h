#pragma once

/**
 * @file SkeletonConverter.h
 * @brief Converter for USD SkelSkeleton prims into a generic SkeletonDescription format that can be further processed
 * into engine-specific skeleton assets.
 *
 * The converter extracts the joint hierarchy, rest poses, bind poses and inverse bind poses
 * of the skeleton, as well as the skinning targets (meshes) that are bound to the skeleton.
 *
 * The actual mesh data of the skinning targets is extracted in the UsdMeshConverter and provided as part of the
 * SkinTargetDescription.
 */

#include <optional>

#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/skinningQuery.h>
#include <pxr/usd/usdSkel/bindingAPI.h>

#include "../types/TargetTypes.h"
#include "TypeConverter.h"

namespace idtxflow
{
namespace converter
{
/**
 * Definition of a Bone of a skeleton.
 * @tparam TargetEngine
 */
template <typename TargetEngine> struct Bone
{
    std::string Name;
    int32_t parentIndex = -1;
    typename idtxflow::types::TargetEngineTypes<TargetEngine>::Transform restTransform;
    typename idtxflow::types::TargetEngineTypes<TargetEngine>::Transform bindPose;
    typename idtxflow::types::TargetEngineTypes<TargetEngine>::Transform inverseBindPose;
};

/**
 * Definition of a Skinning Target of the skeleton. This is the Mesh that will be deformed using the bones o f the
 * skeleton.
 * @tparam TargetEngine
 */
template <typename TargetEngine> struct SkinTargetDescription
{
    std::string Name;
    typename idtxflow::types::TargetEngineTypes<TargetEngine>::Transform GeomBindingTransform;
    std::vector<MeshDescription<typename idtxflow::types::TargetEngineTypes<TargetEngine>::MeshData>> MeshDescriptions;
};

/**
 * Definition of a skeleton with all it's Bones and Skinning Targets.
 * @tparam TargetEngine
 */
template <typename TargetEngine> struct SkeletonDescription
{
    std::string Name;
    std::vector<Bone<TargetEngine>> Bones;
    std::vector<SkinTargetDescription<TargetEngine>> SkinTargets;
};

template <typename TargetEngine>
    requires idtxflow::types::ValidTargetEngine<TargetEngine>
class UsdSkeletonConverter
{
  public:
    using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
    using TypeConverter = UsdTypeConverter<TargetEngine>;
    using MeshDataType = typename Types::MeshData;

    /**
     * Conversion method that creates the SkeletonDecription from an USD SkelRoot and its Skeleton
     * @param usdSkelRoot
     * @param usdSkeleton
     * @return
     */
    SkeletonDescription<TargetEngine> Convert(const pxr::UsdSkelRoot& usdSkelRoot,
                                              const pxr::UsdSkelSkeleton& usdSkeleton)
    {
        // for easy access of skeleton data using queries an UsdSkelCache is required to be constructed
        pxr::UsdSkelCache skelCache;
        skelCache.Populate(usdSkelRoot, pxr::Usd_PrimFlagsPredicate());
        // get the skeleton query to request skeleton data
        pxr::UsdSkelSkeletonQuery skelQuery = skelCache.GetSkelQuery(usdSkeleton);
        if (!skelQuery) return {};

        // extract the skeleton joints with their hierarchy and topology
        pxr::VtArray<class pxr::TfToken> joints = skelQuery.GetJointOrder();
        pxr::UsdSkelTopology topology = skelQuery.GetTopology();

        // get the local rest transforms of the joints
        pxr::VtArray<class pxr::GfMatrix4d> localRestTransforms;
        if (!skelQuery.ComputeJointLocalTransforms(&localRestTransforms, pxr::UsdTimeCode::Default(), true)) return {};

        // get the joints binding transforms
        pxr::VtArray<class pxr::GfMatrix4d> bindingTransforms;
        skelQuery.GetJointWorldBindTransforms(&bindingTransforms);

        // create the bones from the list of joints
        for (size_t joint_idx = 0; joint_idx < joints.size(); ++joint_idx)
        {
            Bone<TargetEngine> bone;
            bone.Name = joints[joint_idx].GetString();
            bone.parentIndex = topology.GetParent(joint_idx);
            bone.restTransform = TypeConverter::toTransform(localRestTransforms[joint_idx]);
            bone.bindPose = TypeConverter::toTransform(bindingTransforms[joint_idx]);
            bone.inverseBindPose = TypeConverter::toTransform(bindingTransforms[joint_idx].GetInverse());

            bones.push_back(bone);
            boneNameToIndex[bone.Name] = static_cast<int32_t>(joint_idx);
        }

        SkeletonDescription<TargetEngine> skeletionDescription;
        skeletionDescription.Bones = bones;
        skeletionDescription.Name = usdSkeleton.GetPrim().GetName().GetString();

        pxr::UsdSkelBinding usdSkelBinding;
        skelCache.ComputeSkelBinding(usdSkelRoot, usdSkeleton, &usdSkelBinding, pxr::Usd_PrimFlagsPredicate());

        for (const pxr::UsdSkelSkinningQuery& skinQuery: usdSkelBinding.GetSkinningTargets())
        {
            pxr::UsdSkelBindingAPI skelBindingApi(skinQuery.GetPrim());
            if (!skelBindingApi) continue;
            class pxr::GfMatrix4d geomBindingTransform(1.0);
            skelBindingApi.GetGeomBindTransformAttr().Get(&geomBindingTransform);

            SkinTargetDescription<TargetEngine> skinTargetDescription;
            skinTargetDescription.Name = skinQuery.GetPrim().GetName().GetString();
            skinTargetDescription.GeomBindingTransform = TypeConverter::toTransform(geomBindingTransform);
            UsdMeshConverter<TargetEngine> meshConverter;
            skinTargetDescription.MeshDescriptions =
                meshConverter.ConvertSkinnedMesh(skinQuery, skelQuery, boneNameToIndex);

            skeletionDescription.SkinTargets.push_back(skinTargetDescription);
        }

        return skeletionDescription;
    }

  private:
    std::vector<Bone<TargetEngine>> bones;
    std::map<std::string, int32_t> boneNameToIndex;
};
} // namespace converter
} // namespace idtxflow
