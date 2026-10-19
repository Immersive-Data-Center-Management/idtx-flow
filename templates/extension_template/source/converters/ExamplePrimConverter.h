#pragma once
/**
 * @file ExamplePrimConverter.h
 * @brief Skeleton converter demonstrating how to implement IPrimConverter for a
 *        custom USD prim type.
 *
 * Replace "MyCustomPrim" with the actual USD prim type token you want to handle,
 * and implement the Convert() method with your conversion logic.
 */

#include <idtxflow/converter/IPrimConverter.h>
#include <idtxflow_godot/PrimConverterRegistryGodot.h>

#include <godot_cpp/classes/node3d.hpp>

#include "../nodes/ExampleUsdNode3D.h"

class ExamplePrimConverter
    : public idtxflow::converter::IPrimConverter<idtxflow::types::TargetEngineGodot>
{
    using Base = idtxflow::converter::IPrimConverter<idtxflow::types::TargetEngineGodot>;
    using Types = Base::Types;

public:
    using ConvertedEntityHandle = Base::ConvertedEntityHandle;

    std::vector<pxr::TfToken> GetSupportedPrimTypes() const override
    {
        // Return the USD prim type token(s) this converter handles.
        // Multiple tokens are supported if one converter handles several types.
        return { pxr::TfToken("MyCustomPrim") };
    }

    int GetPriority() const override
    {
        // 100 = standard third-party priority.
        // Use higher values to override built-in or other third-party converters.
        return 100;
    }

    std::string GetConverterName() const override
    {
        return "ExamplePrimConverter";
    }

    ConvertedEntityHandle Convert(const pxr::UsdPrim& prim) override
    {
        // TODO: Read USD attributes from `prim` and create the appropriate
        //       Godot node. For example:
        //
        //   auto* node = memnew(ExampleUsdNode3D);
        //   node->set_transform(transform);
        //   // ... set up mesh, materials, etc. from prim attributes ...
        //   return Types::GetConvertedEntityHandle(node);

        // Placeholder: return a minimal IUsdNode3D-compatible node
        auto* node = memnew(ExampleUsdNode3D);
        node->set_name(godot::String(prim.GetName().GetText()));
        // Never return a raw godot-cpp wrapper to IDTXFlow. The ObjectID is
        // resolved to an IDTXFlow-local wrapper after this call returns.
        return Types::GetConvertedEntityHandle(node);
    }

    ConvertedEntityHandle PostProcess(
        const pxr::UsdPrim& prim,
        ConvertedEntityHandle converted,
        ConvertedEntityHandle parent) override
    {
        // Optional: perform setup that depends on the scene hierarchy.
        // Called after parent-child relationships have been established.
        // Resolve handles here to obtain wrappers owned by this GDExtension:
        // godot::Node3D* converted_node = Types::ResolveConvertedEntity(converted);
        // godot::Node3D* parent_node = Types::ResolveConvertedEntity(parent);
        return converted;
    }
};
