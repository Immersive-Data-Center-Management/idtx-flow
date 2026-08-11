#include "../include/idtxflow_godot/nodes/IUsdNode3D.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/editor_inspector.hpp>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usd/primRange.h>

#include <idtxflow_godot/idtxflow_godot_api.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>

using namespace godot;
using namespace pxr;


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

void IDTXFLOW_GODOT_API IUsdNode3D::_apply_variant_change()
{
    godot::UtilityFunctions::print(">>> _apply_variant_change called");
    
    godot::Node3D* self_node = dynamic_cast<godot::Node3D*>(this);
    if (!self_node)
    {
        IDTX_LOGF(IDTX_ERROR, "Failed to cast to Node3D in _apply_variant_change");
        return;
    }
    
    if (!self_node->has_meta("__pending_variant_set"))
    {
        IDTX_LOGF(IDTX_WARN, "No pending variant data found");
        return;
    }
    
    String variant_set = self_node->get_meta("__pending_variant_set");
    String value = self_node->get_meta("__pending_variant_value");
    
    self_node->remove_meta("__pending_variant_set");
    self_node->remove_meta("__pending_variant_value");
      
    if (!stage_node_)
    {
        IDTX_LOGF(IDTX_ERROR, "stage_node is NULL");
        return;
    }
    
    const SdfPath prim_path(prim_path_.utf8().get_data());
    const UsdStageRefPtr stage = stage_node_->get_stage();
    
    if (!stage)
    {
        IDTX_LOGF(IDTX_ERROR, "Stage is NULL");
        return;
    }
    
    UsdPrim prim = stage->GetPrimAtPath(prim_path);
    if (!prim)
    {
        IDTX_LOGF(IDTX_ERROR, "Prim not found");
        return;
    }
    
    // Set variant on session layer
    stage->SetEditTarget(stage->GetSessionLayer());
    UsdVariantSet prim_variant_set = prim.GetVariantSet(variant_set.utf8().get_data());
    if (!prim_variant_set)
    {
        IDTX_LOGF(IDTX_ERROR, "VariantSet '%s' not found", variant_set.utf8().get_data());
        return;
    }
    
    bool success = prim_variant_set.SetVariantSelection(value.utf8().get_data());
     
    if (!success)
    {
        IDTX_LOGF(IDTX_ERROR, "Failed to set variant selection: %s = %s", variant_set.utf8().get_data(), value.utf8().get_data());
        return;
    }
    
    variant_sets_variant_[variant_set] = value;
    
    // Remove all existing payload children
    while (self_node->get_child_count() > 0)
    {
        godot::Node* child = self_node->get_child(0);
        self_node->remove_child(child);
        child->queue_free();
    }
    
    // Force re-compose the prim
    stage->SetEditTarget(stage->GetRootLayer());
    stage->SetEditTarget(stage->GetSessionLayer());
    
    prim = stage->GetPrimAtPath(prim_path);
    
    // Use UsdPrimRange to traverse ALL descendants (including unloaded payloads)
    pxr::UsdPrimRange range = pxr::UsdPrimRange::AllPrims(prim);
    
    for (auto it = range.begin(); it != range.end(); ++it)
    {
        const pxr::UsdPrim& descendant = *it;
        
        if (descendant == prim)
            continue;
        
        // Only process direct children
        if (descendant.GetParent() != prim)
        {
            it.PruneChildren();
            continue;
        }
        
        String child_path = String(descendant.GetPath().GetText());
        godot::Node3D* new_child = stage_node_->convert_prim_at_path(child_path);
        
        if (new_child)
        {
            self_node->add_child(new_child);
            new_child->set_owner(self_node->get_owner());
            
            // Don't set stage_node_ for UsdStageNode3D children (they manage their own stage)
            UsdStageNode3D* stage_child = godot::Object::cast_to<UsdStageNode3D>(new_child);
            if (!stage_child)
            {
                _set_owner_recursive(new_child, self_node->get_owner());
            }
        }
        else
        {
            IDTX_LOGF(IDTX_WARN, "Failed to convert child at path: %s", child_path.utf8().get_data());
        }
        
        it.PruneChildren();
    }
}

void IDTXFLOW_GODOT_API IUsdNode3D::_set_owner_recursive(godot::Node* node, godot::Node* owner)
{
    if (!node || !owner) return;
      
    for (int i = 0; i < node->get_child_count(); i++)
    {
        godot::Node* child = node->get_child(i);
        child->call_deferred("set_owner", owner);
         
        IUsdNode3D* usd_child = IUsdNode3D::from_node(child);
        if (usd_child)
        {
            usd_child->set_stage_node(stage_node_);
        }
        
        _set_owner_recursive(child, owner);
    }
}

String IDTXFLOW_GODOT_API IUsdNode3D::get_active_variantset_name() const
{
    if (variant_sets_.is_empty())
        return String();
    
    return variant_sets_.keys()[0];
}

String IUsdNode3D::get_active_variant() const
{
    String varset_name = get_active_variantset_name();
    if (varset_name.is_empty()) return String();
    
    if (variant_sets_variant_.has(varset_name))
        return variant_sets_variant_[varset_name];
    
    return String();
}

void IUsdNode3D::set_active_variant(const String& value)
{
    String varset_name = get_active_variantset_name();
    if (varset_name.is_empty()) return;
    
    variant_sets_variant_[varset_name] = value;
    
    Node3D* self_node = dynamic_cast<Node3D*>(this);
    if (!self_node) return;
    
    self_node->set_meta("__pending_variant_set", varset_name);
    self_node->set_meta("__pending_variant_value", value);
    self_node->call_deferred("_apply_variant_change");
}

String IUsdNode3D::get_active_variant_hint() const
{
    String varset_name = get_active_variantset_name();
    if (varset_name.is_empty()) return String();
    
    if (!variant_sets_.has(varset_name))
        return String();
    
    PackedStringArray variants = variant_sets_[varset_name];
    String hint = "";
    for (int i = 0; i < variants.size(); i++)
    {
        if (i > 0) hint += ",";
        hint += variants[i];
    }
    return hint;
}

void IDTXFLOW_GODOT_API IUsdNode3D::extract_variant_sets_from_prim(const pxr::UsdPrim& prim)
{
    if (!prim.HasVariantSets())
    {
        return;
    }
    
    pxr::UsdVariantSets variantSets = prim.GetVariantSets();
    std::vector<std::string> setNames = variantSets.GetNames();
     
    for (const std::string& setName : setNames)
    {
        pxr::UsdVariantSet variantSet = prim.GetVariantSet(setName);
        std::vector<std::string> variantNames = variantSet.GetVariantNames();
                
        godot::PackedStringArray godot_variants;
        for (const std::string& variantName : variantNames)
        {
            godot_variants.push_back(godot::String(variantName.c_str()));
        }
        
        godot::String set_name_godot(setName.c_str());
        variant_sets_[set_name_godot] = godot_variants;
        
        std::string currentSelection = variantSet.GetVariantSelection();
        if (!currentSelection.empty())
        {
            variant_sets_variant_[set_name_godot] = godot::String(currentSelection.c_str());
        }
    }
       
    if (godot::Node* node = dynamic_cast<godot::Node*>(this))
    {
        node->notify_property_list_changed();
    }
}

void IDTXFLOW_GODOT_API IUsdNode3D::populate_variant_properties(
    godot::List<godot::PropertyInfo>* p_list) const
{
    String hint = get_active_variant_hint();
    
    if (!hint.is_empty())
    {
        p_list->push_back(godot::PropertyInfo(
            godot::Variant::STRING,
            "active_variant",
            godot::PROPERTY_HINT_ENUM,
            hint,
            godot::PROPERTY_USAGE_DEFAULT
        ));
    }
}