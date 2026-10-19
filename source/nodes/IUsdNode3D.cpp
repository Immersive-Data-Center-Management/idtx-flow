#include "../include/idtxflow_godot/nodes/IUsdNode3D.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/editor_inspector.hpp>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/variantSets.h>

#include <idtxflow_godot/idtxflow_godot_api.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>

using namespace godot;
using namespace pxr;

PackedStringArray IDTXFLOW_GODOT_API IUsdNode3D::get_variantset_variants(const String& name) const
{
    if (variant_sets_.has(name)) return variant_sets_[name];

    return godot::PackedStringArray();
}

void IDTXFLOW_GODOT_API IUsdNode3D::set_variantset_variants(const String& name, const PackedStringArray& values)
{
    variant_sets_[name] = values;
}

IDTXFLOW_GODOT_API IUsdNode3D::~IUsdNode3D()
{
    stage_node_ = nullptr;
}

Dictionary IDTXFLOW_GODOT_API IUsdNode3D::get_variantsets() const
{
    return variant_sets_;
}

void IDTXFLOW_GODOT_API IUsdNode3D::set_variantsets(const Dictionary& variant_sets)
{
    variant_sets_ = variant_sets;
}

String IDTXFLOW_GODOT_API IUsdNode3D::get_variantset_selected_variant(const String& variant_set) const
{
    if (variant_sets_variant_.has(variant_set)) return variant_sets_variant_[variant_set];

    return String();
}

void IDTXFLOW_GODOT_API IUsdNode3D::set_variantset_selected_variant(const String& variant_set, const String& value,
                                                                    bool is_converting)
{
    // when we are setting the selected variant from convertion mode we just set the value...
    if (is_converting)
    {
        variant_sets_variant_[variant_set] = value;
        return;
    }
    // when choosing a new variant of the variant set we need to revisit the originating
    // prim and configure its variant and reload/re-compose the prim, as the variant may
    // change its coloring, material, mesh or complete prim-tree
    if (stage_node_)
    {
        const SdfPath prim_path(prim_path_.utf8().get_data());
        const UsdStageRefPtr stage = stage_node_->get_stage();
        const UsdPrim prim = stage->GetPrimAtPath(prim_path);
        if (!prim) return;
        // before switching the variant ensure we have the session layer of the stage to be the edit target
        stage->SetEditTarget(stage->GetSessionLayer());

        UsdVariantSet prim_variant_set = prim.GetVariantSet(variant_set.utf8().get_data());
        prim_variant_set.SetVariantSelection(value.utf8().get_data());

        // re-convert the prim into the current node... re-fetching the prim from the stage ensures the composition
        // happens with the variant just selected
        // TODO: implement single prim conversion after variant selection to compose the new selected variant
        // properties!
    }
}