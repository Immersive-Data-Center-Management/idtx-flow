#pragma once

/**
 * @file AnimationConverter.h
 * @brief Converter for USD animation data (primarily UsdGeomXformable and UsdSkelSkeleton) into a generic AnimationDescription format
 * that can be further processed into engine-specific animation assets.
 */
#include <map>
#include <optional>
#include <variant>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/animQuery.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/vt/array.h>

#include "../types/TargetTypes.h"
#include "TypeConverter.h"

namespace idtxflow
{
namespace converter
{
    enum AnimationLoopType
    {
        LOOP_NONE,
    };

    enum AnimationTrackType
    {
        TRACK_POSITION,
        TRACK_ROTATION,
        TRACK_SCALE,
        TRACK_TRANSFORM,
        TRACK_BLEND_WEIGHT  // Blend shape weight (Godot range -1..1)
    };

    template<typename TargetEngine>
    struct AnimationTrackKey
    {
        using Vector3 = typename idtxflow::types::TargetEngineTypes<TargetEngine>::Vector3;
        using Quaternion = typename idtxflow::types::TargetEngineTypes<TargetEngine>::Quaternion;
        using Transform = typename idtxflow::types::TargetEngineTypes<TargetEngine>::Transform;
        
        double Time;
        std::variant<Vector3, Quaternion, Transform, float> Value;
    };

    template<typename TargetEngine>
    struct AnimationTrackDescription
    {
        std::string Name;
        AnimationTrackType Type;
        std::vector<AnimationTrackKey<TargetEngine>> Keys;
    };

    template<typename TargetEngine>
    struct AnimationDescription
    {
        float Length;
        AnimationLoopType LoopType;
        std::vector<AnimationTrackDescription<TargetEngine>> Tracks;
    };
    
    template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
    class UsdAnimationConverter
    {
    public:
        using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
        using TypeConverter = UsdTypeConverter<TargetEngine>;
        
        std::optional<AnimationDescription<TargetEngine>> Convert(const pxr::UsdGeomXformable& usdXForm, double usdTimeCodesPerSec)
        {
            AnimationDescription<TargetEngine> animation;
            animation.LoopType = LOOP_NONE;

            // usd xformables will use a single track
            AnimationTrackDescription<TargetEngine> track;
            track.Type = TRACK_TRANSFORM;

            // extract animation data from the usd transformable if any of the transform components are authored as
            // timevarying values
            if (!usdXForm.TransformMightBeTimeVarying()) return {};

            // retrieve the time code samples from the prim and "bake" their values into the animation description
            std::vector<double> xform_timecodes;
            bool resets;
            usdXForm.GetTimeSamples(&xform_timecodes);
            for (double timecode : xform_timecodes)
            {
                // retrieve the combined transform at the specific timecode
                class pxr::GfMatrix4d tc_matrix;
                if (!usdXForm.GetLocalTransformation(&tc_matrix, &resets, pxr::UsdTimeCode(timecode)))
                    continue;

                // depending on the animation resolution calculate the actual sample time in seconds
                double timesample = timecode / usdTimeCodesPerSec;
                AnimationTrackKey<TargetEngine> key;
                key.Time = timesample;
                key.Value = TypeConverter::toTransform(tc_matrix);
                track.Keys.push_back(key);
            }

            animation.Tracks.push_back(track);

            return animation;
        }

        std::optional<AnimationDescription<TargetEngine>> Convert(const pxr::UsdSkelRoot& usdSkelRoot, const pxr::UsdSkelSkeleton& usdSkeleton, double usdTimeCodesPerSec)
        {
            // for easy access of skeleton data using queries an UsdSkelCache is required to be constructed
            pxr::UsdSkelCache skelCache;
            skelCache.Populate(usdSkelRoot, pxr::Usd_PrimFlagsPredicate());
            // get the skeleton query to request skeleton data
            pxr::UsdSkelSkeletonQuery skelQuery = skelCache.GetSkelQuery(usdSkeleton);
            if (!skelQuery) return {};

            pxr::UsdSkelAnimQuery animQuery = skelQuery.GetAnimQuery();
            if (!animQuery) return {};

            // retrieve the animation data of the skeleton
            pxr::VtArray<class pxr::TfToken> animJoints = animQuery.GetJointOrder();
            std::vector<pxr::UsdAttribute> animAttributes;
            animQuery.GetJointTransformAttributes(&animAttributes);
            std::vector<double> animTimecodes;
            animQuery.GetJointTransformTimeSamples(&animTimecodes);

            AnimationDescription<TargetEngine> animation;
            animation.LoopType = LOOP_NONE;

            // wee need to create animation tracks for each bone and each track type
            // openUSD stores the key frames for all bones/joints in an array for each animation attribute
            // the type of the array differs based on the animation attribute. So run through the animation
            // attributes and create a track for each attribute and each bone
            size_t trackOffset = 0;
            for (const pxr::UsdAttribute& animAttribute : animAttributes)
            {
                // if this attribute has no time samples, we do not create any track
                if (animAttribute.GetNumTimeSamples() == 0) continue;
                
                AnimationTrackDescription<TargetEngine> track;
                
                if (animAttribute.GetName() == pxr::TfToken("translations"))
                    track.Type = TRACK_POSITION;
                else if (animAttribute.GetName() == pxr::TfToken("rotations"))
                    track.Type = TRACK_ROTATION;
                else if (animAttribute.GetName() == pxr::TfToken("scales"))
                    track.Type = TRACK_SCALE;
                else
                    // other animated attributes do not create any tracks
                    continue;
                
                // This attribute's tracks start where the list currently ends;
                // accumulating sizes instead indexes past the vector once a
                // third attribute arrives.
                trackOffset = animation.Tracks.size();

                // for each bone add an empty track for this track type
                // the order of key frames in the authored timecodes is stable and matches
                // the order of joints in this array.
                for (const class pxr::TfToken& animJoint : animJoints)
                {
                    track.Name = animJoint.GetString();
                    animation.Tracks.push_back(track);
                }

                for (double timecode : animTimecodes)
                {
                    switch (track.Type) {
                    case TRACK_ROTATION:
                        {
                            pxr::VtArray<class pxr::GfQuatf> jointKeys;
                            if (animAttribute.Get(&jointKeys, pxr::UsdTimeCode(timecode)))
                            {
                                double timesample = timecode / usdTimeCodesPerSec;
                                for (size_t j = 0; j < jointKeys.size(); ++j)
                                {
                                    AnimationTrackKey<TargetEngine> key;
                                    key.Time = timesample;
                                    key.Value = TypeConverter::toQuaternion(jointKeys[j]);
                                    animation.Tracks[j + trackOffset].Keys.push_back(key);
                                }
                            }
                            break;
                        }
                    case TRACK_POSITION:
                    case TRACK_SCALE:
                        {
                            pxr::VtArray<class pxr::GfVec3f> jointKeys;
                            if (animAttribute.Get(&jointKeys, pxr::UsdTimeCode(timecode)))
                            {
                                double timesample = timecode / usdTimeCodesPerSec;
                                for (size_t j = 0; j < jointKeys.size(); ++j)
                                {
                                    AnimationTrackKey<TargetEngine> key;
                                    key.Time = timesample;
                                    key.Value = TypeConverter::toVector3(jointKeys[j]);
                                    animation.Tracks[j + trackOffset].Keys.push_back(key);
                                }
                            }
                            break;
                        }
                    default:
                        break;
                    }
                }

            }

            // Extract blend shape weight animation from the same animQuery.
            // Each blend shape becomes one TRACK_BLEND_WEIGHT track whose keys
            // are (time, weight) pairs.  We reuse the joint-transform time
            // samples (already baked above) -- both sets live on the same
            // UsdSkelAnimation prim, so identical time codes resolve correctly.
            // If the joint animation has no time samples, we emit a single
            // rest-pose key so downstream code has a consistent track structure.
            std::vector<double> blendTimecodes;
            animQuery.GetBlendShapeWeightTimeSamples(&blendTimecodes);
            if (blendTimecodes.empty())
                blendTimecodes = animTimecodes;
            if (blendTimecodes.empty())
            {
                blendTimecodes.push_back(0.0);
            }

            pxr::VtArray<pxr::TfToken> blendOrder = animQuery.GetBlendShapeOrder();
            if (!blendOrder.empty())
            {
                const size_t numBlends = blendOrder.size();
                const size_t blendTrackStart = animation.Tracks.size();
                // Reserve a TRACK_BLEND_WEIGHT track for each blend shape.
                // The Name holds the blend shape's token name.
                for (const pxr::TfToken& bname : blendOrder)
                {
                    AnimationTrackDescription<TargetEngine> bt;
                    bt.Type = TRACK_BLEND_WEIGHT;
                    bt.Name = bname.GetString();
                    animation.Tracks.push_back(std::move(bt));
                }

                // Sample the blend weights at each timecode.
                pxr::VtArray<float> blendWeights;
                for (double tc : blendTimecodes)
                {
                    if (!animQuery.ComputeBlendShapeWeights(&blendWeights, pxr::UsdTimeCode(tc)))
                        continue;
                    if (blendWeights.size() < numBlends)
                        continue;
                    // Timesample in seconds.
                    const double ts = tc / usdTimeCodesPerSec;
                    for (size_t b = 0; b < numBlends; ++b)
                    {
                        AnimationTrackKey<TargetEngine> key;
                        key.Time = ts;
                        key.Value = blendWeights[b];
                        animation.Tracks[blendTrackStart + b].Keys.push_back(std::move(key));
                    }
                }
            }

            return animation;
        }
    };
}
}