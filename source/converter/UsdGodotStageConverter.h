#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include <idtxflow/converter/TypeConverter.h>
#include <idtxflow/converter/StageConverter.h>
#include <idtxflow/converter/PrimConverterRegistry.h>

#include <idtxflow_godot/types/GodotTypes.h>

#include "nodes/UsdMeshInstanceNode3D.h"
#include "nodes/UsdXFormNode3D.h"
#include "nodes/UsdMultiMeshInstanceNode3D.h"

/**
 * Implement Godot engine specialization of the UsdStageConverter
 **/
namespace idtxflow
{
namespace helper
{
    template<typename NodeType>
        requires requires(NodeType* n, godot::Ref<godot::Animation> anim) { n->set_animation(anim); }
    inline void AddAnimation(
        const converter::AnimationDescription<types::TargetEngineGodot>& animation,
        NodeType* node,
        double animation_length)
    {
        godot::Ref<godot::Animation> godot_animation;
        godot_animation.instantiate();
        godot_animation->set_length(static_cast<float>(animation_length));
        godot_animation->set_loop_mode(godot::Animation::LOOP_NONE);

        // in the XForm animation case we expect only one Transform track to be extracted from USD
        if (!animation.Tracks.empty())
        {
            for (const auto& track: animation.Tracks)
            {
                if (track.Keys.empty()) continue;
                
                switch (track.Type)
                {
                case converter::TRACK_TRANSFORM:
                    {
                        // a transform track need to be split into seperate animation tracks in godot for
                        // position, rotation and scale
                        int32_t pos_track = godot_animation->add_track(godot::Animation::TYPE_POSITION_3D);
                        godot_animation->track_set_path(pos_track, godot::NodePath(track.Name.c_str()));
                        int32_t rot_track = godot_animation->add_track(godot::Animation::TYPE_ROTATION_3D);
                        godot_animation->track_set_path(rot_track, godot::NodePath(track.Name.c_str()));
                        int32_t scale_track = godot_animation->add_track(godot::Animation::TYPE_SCALE_3D);
                        godot_animation->track_set_path(scale_track, godot::NodePath(track.Name.c_str()));
                        
                        for (const auto& key : track.Keys)
                        {
                            godot::Transform3D animTransform = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Transform>(key.Value);
                            godot_animation->position_track_insert_key(pos_track, key.Time, animTransform.origin);
                            godot_animation->rotation_track_insert_key(rot_track, key.Time, animTransform.basis.get_rotation_quaternion());
                            godot_animation->scale_track_insert_key(scale_track, key.Time, animTransform.basis.get_scale());
                        }
                        
                        break;
                    }
                    case converter::TRACK_POSITION:
                    {
                        int32_t pos_track = godot_animation->add_track(godot::Animation::TYPE_POSITION_3D);
                        godot_animation->track_set_path(pos_track, godot::NodePath(track.Name.c_str()));
                        for (const auto& key : track.Keys)
                        {
                            godot::Vector3 anim_pos = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Vector3>(key.Value);
                            godot_animation->position_track_insert_key(pos_track, key.Time, anim_pos);
                        }
                        break;
                    }
                    case converter::TRACK_ROTATION:
                    {
                        int32_t rot_track = godot_animation->add_track(godot::Animation::TYPE_ROTATION_3D);
                        godot_animation->track_set_path(rot_track, godot::NodePath(track.Name.c_str()));
                        for (const auto& key : track.Keys)
                        {
                            godot::Quaternion anim_rot = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Quaternion>(key.Value);
                            godot_animation->rotation_track_insert_key(rot_track, key.Time, anim_rot);
                        }
                        break;
                    }
                    case converter::TRACK_SCALE:
                    {
                        int32_t scl_track = godot_animation->add_track(godot::Animation::TYPE_SCALE_3D);
                        godot_animation->track_set_path(scl_track, godot::NodePath(track.Name.c_str()));
                        for (const auto& key : track.Keys)
                        {
                            godot::Vector3 anim_scl = std::get<types::TargetEngineTypes<types::TargetEngineGodot>::Vector3>(key.Value);
                            godot_animation->scale_track_insert_key(scl_track, key.Time, anim_scl);
                        }
                        break;
                    }
                }
            }
        } 
        
        node->set_animation(godot_animation);
    }
}

namespace converter
{
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertXform(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation)
    {
        UsdXformNode3D* converted_node = memnew(UsdXformNode3D);
        converted_node->set_transform(transform);

        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }

        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertCube(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<godot::Ref<godot::StandardMaterial3D>>& material,
        float cube_size,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::BoxMesh> box;
        box.instantiate();
        box->set_size(godot::Vector3(cube_size, cube_size, cube_size));

        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        box->set_material(standard_material);
        
        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(box);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }

    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertCylinder(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<godot::Ref<godot::StandardMaterial3D>>& material,
        float cylinder_radius,
        float cylinder_height,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::CylinderMesh> cylinder;
        cylinder.instantiate();
        
        cylinder->set_top_radius(cylinder_radius);
        cylinder->set_bottom_radius(cylinder_radius);
        cylinder->set_height(cylinder_height);

        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        cylinder->set_material(standard_material);

        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(cylinder);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }

    template<>
    inline godot::Node3D* converter::UsdStageConverter<types::TargetEngineGodot>::ConvertCone(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<godot::Ref<godot::StandardMaterial3D>>& material,
        float cone_radius,
        float cone_height,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::CylinderMesh> cylinder;
        cylinder.instantiate();
        
        cylinder->set_top_radius(0.0);
        cylinder->set_bottom_radius(cone_radius);
        cylinder->set_height(cone_height);

        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        cylinder->set_material(standard_material);

        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(cylinder);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }

    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertSphere(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::optional<typename Types::Material>& material,
        float sphere_radius,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::SphereMesh> sphere;
        sphere.instantiate();
        sphere->set_radius(sphere_radius);
        sphere->set_height(sphere_radius * 2.0f);
        
        godot::Ref<godot::StandardMaterial3D> standard_material;
        if (material.has_value())
        {
            standard_material = material.value();
        } else
        {
            // use a default material if none could be created
            standard_material.instantiate();
            // if the current Mesh prim authored a display color as a constant value, pass this into the
            // default material as base albedo color.
            if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
            {
                godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                standard_material->set_albedo(albedo);
                if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
            } else
                standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        }
        
        sphere->set_material(standard_material);
        
        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(sphere);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertMesh(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const std::vector<MeshDescription<UsdMeshConverter<types::TargetEngineGodot>::MeshDataType>>& mesh_descriptions,
        const pxr::VtArray<class pxr::GfVec4f>& display_colors,
        const class pxr::TfToken& color_interpolation)
    {
        godot::Ref<godot::ArrayMesh> mesh;
        mesh.instantiate();
        
        for (size_t MeshSection = 0; MeshSection < mesh_descriptions.size(); ++MeshSection)
        {
            godot::Array mesh_arrays;
            mesh_arrays.resize(godot::Mesh::ARRAY_MAX);
            mesh_arrays[godot::Mesh::ARRAY_VERTEX] = mesh_descriptions[MeshSection].meshData.Vertices;
            mesh_arrays[godot::Mesh::ARRAY_INDEX] = mesh_descriptions[MeshSection].meshData.Triangles;
            mesh_arrays[godot::Mesh::ARRAY_NORMAL] = mesh_descriptions[MeshSection].meshData.Normals;
            if (!mesh_descriptions[MeshSection].meshData.UVs.is_empty())
                mesh_arrays[godot::Mesh::ARRAY_TEX_UV] = mesh_descriptions[MeshSection].meshData.UVs;
            if (!mesh_descriptions[MeshSection].meshData.VertexColors.is_empty())
                mesh_arrays[godot::Mesh::ARRAY_COLOR] = mesh_descriptions[MeshSection].meshData.VertexColors;

            mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, mesh_arrays);

            godot::Ref<godot::StandardMaterial3D> standard_material;
            std::optional<godot::Ref<godot::StandardMaterial3D>> material = ConvertMaterial(mesh_descriptions[MeshSection].usdMaterial);
            if (material.has_value())
            {
                standard_material = material.value();
            } else
            {
                // use a default material if none could be created
                standard_material.instantiate();
                // if the current Mesh prim authored a display color as a constant value, pass this into the
                // default material as base albedo color.
                if (!display_colors.empty() && color_interpolation == pxr::UsdGeomTokens->constant)
                {
                    godot::Color albedo = TypeConverter::toColor(display_colors[0]);
                    standard_material->set_albedo(albedo);
                    if (albedo.a < 1.0f) standard_material->set_transparency(godot::BaseMaterial3D::TRANSPARENCY_ALPHA);
                } else
                    standard_material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
            }
            
            mesh->surface_set_material(mesh->get_surface_count() - 1, standard_material);
        }
        
        UsdMeshInstanceNode3D* converted_node = memnew(UsdMeshInstanceNode3D);
        converted_node->set_mesh(mesh);
        converted_node->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, converted_node, StageAnimationLength);
        }
        
        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertSkeleton(
        const godot::Transform3D& transform,
        const std::optional<AnimationDescription<types::TargetEngineGodot>>& animation,
        const SkeletonDescription<types::TargetEngineGodot>& skeleton_description)
    {
        using GodotMaterialConverter = UsdMaterialConverter<types::TargetEngineGodot>;
        UsdSkeletonNode3D* skeleton = memnew(UsdSkeletonNode3D);
        
        // construct the skeletons bone hierarchy and store the map between bone name and it's index
        godot::Dictionary bone_name_map;
        for (auto& bone: skeleton_description.Bones)
        {
            std::string boneName = bone.Name;
            // we need to replace special characters in bone names that godot refuses to be allowed
            std::replace(boneName.begin(), boneName.end(), '/', '_');
            int32_t boneIndex = skeleton->add_bone(godot::String(boneName.c_str()));
            // keep track of a map between original bone name and none index (required for efficient bone animation)
            bone_name_map.set(godot::NodePath(bone.Name.c_str()), boneIndex);
            skeleton->set_bone_parent(boneIndex, bone.parentIndex);
            skeleton->set_bone_rest(boneIndex, bone.restTransform);
        }
        skeleton->set_joint_to_bone_map(bone_name_map);
        
        // the skeleton might be skinned by different meshes/skin targets. Create the corresponding MeshInstances
        // used to skin the skeleton
        for (auto& skinTarget: skeleton_description.SkinTargets)
        {
            godot::Ref<godot::Skin> skin;
            skin.instantiate();
            
            for (auto& bone: skeleton_description.Bones)
            {
                std::string boneName = bone.Name;
                // we need to replace special characters in bone names that godot refuses to be allowed
                std::replace(boneName.begin(), boneName.end(), '/', '_');
                godot::Transform3D bindTransform = bone.bindPose.affine_inverse() * skinTarget.GeomBindingTransform;
                skin->add_named_bind(godot::String(boneName.c_str()), bindTransform);
            }
            
            godot::Ref<godot::ArrayMesh> mesh;
            mesh.instantiate();
            
            auto& MeshDescriptions = skinTarget.MeshDescriptions;
            //for (size_t MeshSection = 0; MeshSection < MeshDescriptions.size(); ++MeshSection)
            for (const auto& meshDescription: MeshDescriptions)
            {
                godot::Array mesh_arrays;
                mesh_arrays.resize(godot::Mesh::ARRAY_MAX);
                mesh_arrays[godot::Mesh::ARRAY_VERTEX] = meshDescription.meshData.Vertices;
                mesh_arrays[godot::Mesh::ARRAY_INDEX] = meshDescription.meshData.Triangles;
                mesh_arrays[godot::Mesh::ARRAY_NORMAL] = meshDescription.meshData.Normals;
                if (!meshDescription.meshData.UVs.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_TEX_UV] = meshDescription.meshData.UVs;
                if (!meshDescription.meshData.VertexColors.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_COLOR] = meshDescription.meshData.VertexColors;
                if (!meshDescription.meshData.Bones.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_BONES] = meshDescription.meshData.Bones;
                if (!meshDescription.meshData.Weights.is_empty())
                    mesh_arrays[godot::Mesh::ARRAY_WEIGHTS] = meshDescription.meshData.Weights;
                

                mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, mesh_arrays);

                godot::Ref<godot::StandardMaterial3D> standard_material;
                std::optional<godot::Ref<godot::StandardMaterial3D>> material = ConvertMaterial(meshDescription.usdMaterial);
                if (material.has_value())
                {
                    standard_material =  material.value();
                } else
                {
                    // use a default material if none could be created
                    standard_material.instantiate();
                }
                
                mesh->surface_set_material(mesh->get_surface_count() - 1, standard_material);
            }

            UsdMeshInstanceNode3D* node = memnew(UsdMeshInstanceNode3D);
            node->set_mesh(mesh);
            node->set_skin(skin);
            node->set_skeleton(skeleton);
            node->set_name(skinTarget.Name.c_str());
            node->set_stage_path(Stage->GetRootLayer()->GetRealPath().c_str());
            // TODO: check if required, as all nodes converted from an usdPrim implement IUsdNode3D
            node->set_meta("USD_NODE", true);
            
            node->set_prim_name(skinTarget.Name.c_str());
            node->set_prim_type("Mesh");

            skeleton->add_child(node);
        }
        
        skeleton->reset_bone_poses();
        skeleton->set_transform(transform);
        
        if (animation.has_value())
        {
            const auto& animation_description = animation.value();
            helper::AddAnimation(animation_description, skeleton, StageAnimationLength);
        }
        
        return skeleton;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertGprimPseudoInstance(
        const godot::Transform3D& transform,
        const pxr::UsdGeomGprim& usd_gprim,
        const pxr::SdfPath& usd_prototype_path)
    {
        // when converting pseudo instances, each prim is provided as fully composed version. Thus we need to create
        // the "prototype" from the first appearance of the pseudo instance and only add instances for any following prim
        // assuming they only differ in their transform
        if (StagePrototypeMap.contains(usd_prototype_path))
        {
            // if the prototype has been converted into a multimesh already, use this and add a new instance to it
            // once done, this will not return a converted node, or should it?
            pxr::UsdGeomXformCache xform_cache;
            godot::Transform3D global_instance_transform = TypeConverter::toTransform(xform_cache.GetLocalToWorldTransform(usd_gprim.GetPrim()));
            UsdMultiMeshInstanceNode3D* multimesh_node = static_cast<UsdMultiMeshInstanceNode3D*>(StagePrototypeMap.at(usd_prototype_path));
            multimesh_node->add_instance(global_instance_transform);
            // when re-using the converted prototype we will return nullptr instead of the already converted node
            return nullptr;
        }
        
        // create the instantiable mesh and add this first occurence as the first instance
        UsdMeshInstanceNode3D* mesh_instance = dynamic_cast<UsdMeshInstanceNode3D*>(ConvertGprim(usd_gprim));
        if (!mesh_instance) return nullptr;
        
        UsdMultiMeshInstanceNode3D* converted_node = memnew(UsdMultiMeshInstanceNode3D);
        godot::Ref<godot::MultiMesh> multi_mesh;
        multi_mesh.instantiate();
        multi_mesh->set_transform_format(godot::MultiMesh::TRANSFORM_3D);
        multi_mesh->set_mesh(mesh_instance->get_mesh());
        multi_mesh->set_instance_count(1);
        multi_mesh->set_instance_transform(0, transform);
        
        converted_node->set_multimesh(multi_mesh);
        
        // get the global transform of this first instance and store it in the MultimeshInstance node
        // but calculate it's parent global transform as instances will be actually parented to the MultiMesh instance
        // not to the first item in it
        pxr::UsdGeomXformCache xform_cache;
        godot::Transform3D global_transform = TypeConverter::toTransform(xform_cache.GetLocalToWorldTransform(usd_gprim.GetPrim()));
        converted_node->set_global_base_transform(global_transform * transform.affine_inverse());
        // release the converted prim used to build the MultiMeshInstance entity
        memdelete(mesh_instance);
        
        // store the converted node as prototype for all subsequent calls of this method
        StagePrototypeMap.emplace(usd_prototype_path, static_cast<void*>(converted_node));
        
        return converted_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertPrimWithPayload(
        const pxr::UsdPrim& usd_prim,
        const std::string& payload_uri,
        const godot::Transform3D& transform,
        const pxr::SdfLayerRefPtr& override_layer)
    {
        if (payload_uri.empty()) return nullptr;
        
        // a Prim with an authored payload that is not yet loaded will be treated as "standalone" stage node that
        // takes the payloadURI and runs the conversion of the referred stage on its own.
        UsdStageNode3D* stage_node = memnew(UsdStageNode3D);
        stage_node->set_meta("USD_PARENT_MPU", static_cast<float>(StageMetersPerUnit));
        stage_node->set_meta("USD_PARENT_UP", StageUpAxis.GetString().c_str());
        
        // To be able to compose the authored opinions from the current layer into the playload after it has been loaded
        // we will use the override layer containing this data. However, its contents need to be serialized and persisted
        // with the node to survive packaging and re-instantiation. Thus store content and identifier of the layer in the
        // metadata of the node
        std::string override_layer_content;
        override_layer->ExportToString(&override_layer_content);
        stage_node->set_meta("USD_OVERRIDE_LAYER", override_layer_content.c_str());
        stage_node->set_meta("USD_OVERRIDE_LAYERID", override_layer->GetIdentifier().c_str());
        // setting the stage uri will trigger the loading of the stage and conversion either immediately or
        // on `_ready()` of the node
        stage_node->set_stage_uri(payload_uri.c_str());
        
        // we do not the transform on the node itself as it will be set from the payload contents composed with the
        // override layer!
        
        return stage_node;
    }
    
    template<>
    inline godot::Node3D* UsdStageConverter<types::TargetEngineGodot>::ConvertPrimPostProcess(
        const pxr::UsdPrim& usd_prim,
        godot::Node3D* converted_prim,
        godot::Node3D* converted_parent_prim
    )
    {
        if (converted_prim == nullptr) return nullptr;
        
        // maintain parent-child-relationship
        if (converted_parent_prim != nullptr) converted_parent_prim->add_child(converted_prim);
        
        // set name and add META-Tags:
        converted_prim->set_name(usd_prim.GetName().GetText());
        // TODO: check if required, as all nodes converted from an usdPrim implement IUsdNode3D
        converted_prim->set_meta("USD_NODE", true);
        
        // store data in the shared IUsdNode3D class
        IUsdNode3D* usd_node = IUsdNode3D::from_node(converted_prim);
        // it is an implementation error if the node we convert into does not implement IUsdNode3D
        assert(usd_node != nullptr);
        usd_node->set_prim_name(usd_prim.GetName().GetText());
        usd_node->set_prim_path(usd_prim.GetPath().GetText());
        usd_node->set_prim_type(usd_prim.GetTypeName().GetText());
        usd_node->set_stage_path(Stage->GetRootLayer()->GetRealPath().c_str());
        
        return converted_prim;
    }
    
    template<>
    inline std::vector<godot::Node3D*> UsdStageConverter<types::TargetEngineGodot>::ConvertStagePostProcess(
        const std::vector<godot::Node3D*>& converted_entities)
    {
        // once the whole stage has been converted into respective godot nodes we will apply  any required rotation and
        // scaling based on the authored up-axis and meters-per-units in the stage
        // This seems to be a much more performant way of aligning the whole stage to Godot's coordinate system and
        // orientation compared to re-calculating each individual mesh and it's transforms        
        for (godot::Node3D* node : converted_entities)
        {
            // rotate and scale the converted nodes based on up-axis and MPU settings
            
            if (StageUpAxis == pxr::UsdGeomTokens->z)
            {
                node->rotate_x(static_cast<float>(godot::Math::deg_to_rad(-90.0)));
            } else if (StageUpAxis == pxr::UsdGeomTokens->x)
            {
                node->rotate_z(static_cast<float>(godot::Math::deg_to_rad(90.0)));
            }
            // if this node represents a nested stage it contains metadata about the parents UP axis. In this case
            // we will need to apply the "reverse" rotation of the parent to keep the correct orientation
            if (OwningEntity && OwningEntity->has_meta("USD_PARENT_UP"))
            {
                godot::String parent_up = OwningEntity->get_meta("USD_PARENT_UP");
                std::string parent_up_str = std::string(parent_up.utf8().get_data());
                if (parent_up_str == pxr::UsdGeomTokens->z.GetString())
                {
                    node->rotate_x(static_cast<float>(godot::Math::deg_to_rad(+90.0)));
                } else if (parent_up_str == pxr::UsdGeomTokens->x.GetString())
                {
                    node->rotate_z(static_cast<float>(godot::Math::deg_to_rad(-90.0)));
                }
            }
            
            float mpu = static_cast<float>(StageMetersPerUnit);
            // We will only scale the converted nodes based on MPU if they are not part of a nested stage (via payload).
            // This matches the expectations, that different MPU's in referenced stages/layers need to be adjusted with
            // scaling of the prim, that refers a layer with different MPU setting.
            if (!OwningEntity || !OwningEntity->has_meta("USD_PARENT_MPU"))
            {
                godot::Vector3 scale = node->get_scale() * mpu;
                node->set_scale(scale);
            }
        }
        
        return converted_entities;
    }
}
    

}
