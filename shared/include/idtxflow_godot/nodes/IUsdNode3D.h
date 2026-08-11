#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/print_string.hpp>

#include <idtxflow_godot/idtxflow_godot_api.h>

#include <pxr/usd/usd/prim.h>

class UsdStageNode3D;

/**
 * The IUsdNode3D class is used to provide common functionality and features that are shared accress all
 * Usd nodes that will inherit from different Godot standard classes. This class addresses the diamond
 * issue that we can't inject a custom class in the inheritance chain of existing Godot classes but still
 * want to provide shared/core behavior for all of them
 */
class IDTXFLOW_GODOT_API IUsdNode3D
{
public:
    IUsdNode3D() = default;
    virtual ~IUsdNode3D();

    /**
     * Always use this over `dynamic_cast<IUsdNode3D*>`!
     * 
     * Due to cross DLL/DYLIB boundry issues with dynamic_cast<>, we will store a safe uint64 pointer as
     * meta data to this node during construction - see `IUSDNODE` macro. This static method allows to
     * safely receive the properly typed node pointer accross DLL/DYLIB boundries in case node are instantiated
     * within extensions to this plugin.
     * @param node 
     * @return 
     */
    static IUsdNode3D* from_node(godot::Node* node)
    {
        if (!node || !node->has_meta("__iusdnode3d_ptr")) return nullptr;
        return reinterpret_cast<IUsdNode3D*>(
            static_cast<uintptr_t>(static_cast<uint64_t>(node->get_meta("__iusdnode3d_ptr")))
        );
    }
    
    /**
     * Store the StageNode3D reference that "owns" this node that has been converted from an UsdPrim
     * @param node The Reference to the StageNode
     */
    void set_stage_node(UsdStageNode3D* node) { stage_node_ = node; }
    
    /**
     * Getter for the path to the stage referred to by the stage_node_.
     * @return prim_path
     */
    virtual godot::String get_stage_path() const { return  stage_path_; }

    /**
     * Setter for the path of the stage referred to by the stage_node_.
     * @param stage_path 
     */
    virtual void set_stage_path(const godot::String& stage_path) { stage_path_ = stage_path; }

    /**
     * Getter for the name of the prim this node has been converted from
     * @return 
     */
    virtual godot::String get_prim_name() const { return  prim_name_; }

    /**
     * Setter for the name of the prim this node has been converted from
     * @param prim_name 
     */
    virtual void set_prim_name(const godot::String& prim_name) { prim_name_ = prim_name; }

    /**
     * Getter for the path to the prim within the stage_node_ this UsdNode3D is converted from.
     * Use `stage_node_->GetPrimAtPath(prim_path)` to retrieve the prim from the stage.
     * @return prim_path
     */
    virtual godot::String get_prim_path() const { return  prim_path_; }

    /**
     * Setter for the path of the prim within the stage_node_ this UsdNode3D is converted from.
     * @param prim_path 
     */
    virtual void set_prim_path(const godot::String& prim_path) { prim_path_ = prim_path; }

    /**
     * Setter for the prim type name of the prim this node has been converted from
     * @return 
     */
    virtual godot::String get_prim_type() const { return  prim_type_; }

    /**
     * Setter for the prim type name of the prim this node has been converted from
     * @param prim_type 
     */
    virtual void set_prim_type(const godot::String& prim_type) { prim_type_ = prim_type; }

    /**
     * Return the list of variant sets with all their variants
     * @return 
     */
    virtual godot::Dictionary get_variantsets() const;

    /**
     * Set all variant sets
     * @param variant_sets Dictionary of variant set definitions
     */
    virtual void set_variantsets(const godot::Dictionary& variant_sets);

    /**
    * Extract variant sets from a USD prim and store them internally.
    * Populates both available variants and current selections.
    * 
    * @param prim The USD prim to extract variants from
    */
    void extract_variant_sets_from_prim(const pxr::UsdPrim& prim);

    /**
    * Determine which variant set is "active" for the inspector dropdown.
    * 
    * @return Name of the active variant set, or empty if none exist
    */
    virtual godot::String get_active_variantset_name() const;

    /**
    * Get the currently selected variant for the active variant set.
    * Used by the inspector to display the current value.
    * 
    * @return Selected variant name 
    */
    virtual godot::String get_active_variant() const;

    /**
    * Set the selected variant for the active variant set.
    * Triggers a deferred reload of USD children with the new variant.
    * 
    * @param value The variant to select
    */
    virtual void set_active_variant(const godot::String& value);

    /**
    * Get the enum hint string for the inspector dropdown.
    * 
    * @return Comma-separated variant names 
    */
    virtual godot::String get_active_variant_hint() const;

    /**
    * Populate Godot Inspector with variant set properties 
    * @param p_list List to append property info to
    */
    void populate_variant_properties(godot::List<godot::PropertyInfo>* p_list) const;

protected:
    /**
     * Apply a pending variant change (called via call_deferred).
     */
    virtual void _apply_variant_change();

   /**
     * Return the list of all variant sets and their currently selected variant
     * @return 
     */
    virtual godot::Dictionary get_variantsets_selected_variants() const { return variant_sets_variant_; }

    /**
     * Store the list of all variants sets and their selected variant
     * @param sets_values 
     */
    virtual void set_variantsets_selected_variants(const godot::Dictionary& sets_values) { variant_sets_variant_ = sets_values; }

    /**
    * Recursively set owner for this node and all descendants in the scene tree
    * @param node Starting node
    * @param owner Owner node to assign
    */
    virtual void _set_owner_recursive(godot::Node* node, godot::Node* owner);
 
    // store a reference to the stage node that created this node
    UsdStageNode3D* stage_node_ = nullptr;
    // store the path to the stage the usdPrim originated from
    godot::String stage_path_;
    // store the path to the UsdPrim this node was created from
    godot::String prim_path_;
    // store the name of the UsdPrim this node was created from
    godot::String prim_name_;
    // store the type name of the UsdPrim this node was created from
    godot::String prim_type_;
    
    // list of available variant sets. Key is the variant-set name
    // value is a set of variants assigned to this variant set
    godot::Dictionary variant_sets_;
    // list of variant sets and their actual selected variant
    godot::Dictionary variant_sets_variant_;
};

#define IUSDNODE(m_class) /*** Define this class as an IUSDNode3D class ****/ \
public: \
    m_class() { \
        set_meta("__iusdnode3d_ptr", \
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<IUsdNode3D*>(this)))); \
    }\
    ~m_class() override = default; \
    bool _set(const godot::StringName &p_name, const godot::Variant &p_value) {\
        /* we never set the deserialized metadata value for this internal key. */\
        /* otherwise this would overwrite the value created in the construct */\
        if (p_name == godot::StringName("metadata/__iusdnode3d_ptr")) return true;\
        return false;\
    }\
    IUSDNODE_IMPLEMENT_GETTER_SETTER \

#define IUSDNODE_IMPLEMENT_GETTER_SETTER /*** Implement "redirect" to IUsdNode3D setter/getter to be used in Binding *****/ \
public:\
    godot::String get_stage_path() const override { return IUsdNode3D::get_stage_path(); } \
    void set_stage_path(const godot::String& stage_path) override { IUsdNode3D::set_stage_path(stage_path); } \
    godot::String get_prim_path() const override { return IUsdNode3D::get_prim_path(); } \
    void set_prim_path(const godot::String& prim_path) override { IUsdNode3D::set_prim_path(prim_path); } \
    godot::String get_prim_name() const override { return IUsdNode3D::get_prim_name(); } \
    void set_prim_name(const godot::String& prim_name) override { IUsdNode3D::set_prim_name(prim_name); } \
    godot::String get_prim_type() const override { return IUsdNode3D::get_prim_type(); } \
    void set_prim_type(const godot::String& prim_type) override { IUsdNode3D::set_prim_type(prim_type); } \
    godot::Dictionary get_variantsets() const override { return IUsdNode3D::get_variantsets(); } \
    void set_variantsets(const godot::Dictionary& variant_sets) override { IUsdNode3D::set_variantsets(variant_sets); } \
    godot::String get_active_variant() const override { return IUsdNode3D::get_active_variant(); } \
    void set_active_variant(const godot::String& value) override { IUsdNode3D::set_active_variant(value); }\
    void _apply_variant_change() override { IUsdNode3D::_apply_variant_change(); }\
protected: \
    godot::Dictionary get_variantsets_selected_variants() const override { return IUsdNode3D::get_variantsets_selected_variants(); } \
    void set_variantsets_selected_variants(const godot::Dictionary& sets_values) override { IUsdNode3D::set_variantsets_selected_variants(sets_values); }

#define IUSDNODE_IMPLEMENT_BINDINGS(m_class) /*** Implement Binding-Code for the properties provided by the IUsdNode interface ***/ \
    godot::ClassDB::bind_method(godot::D_METHOD("get_stage_path"), &m_class::get_stage_path); \
    godot::ClassDB::bind_method(godot::D_METHOD("set_stage_path", "path"), &m_class::set_stage_path); \
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "stage_path", \
        godot::PROPERTY_HINT_NONE, "" , \
        godot::PROPERTY_USAGE_STORAGE | godot::PROPERTY_USAGE_EDITOR | godot::PROPERTY_USAGE_READ_ONLY ), \
"set_stage_path", "get_stage_path"); \
\
godot::ClassDB::bind_method(godot::D_METHOD("get_prim_path"), &m_class::get_prim_path); \
    godot::ClassDB::bind_method(godot::D_METHOD("set_prim_path", "path"), &m_class::set_prim_path); \
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "prim_path", \
        godot::PROPERTY_HINT_NONE, "" , \
        godot::PROPERTY_USAGE_STORAGE | godot::PROPERTY_USAGE_EDITOR | godot::PROPERTY_USAGE_READ_ONLY ), \
        "set_prim_path", "get_prim_path"); \
\
    godot::ClassDB::bind_method(godot::D_METHOD("get_prim_name"), &m_class::get_prim_name); \
    godot::ClassDB::bind_method(godot::D_METHOD("set_prim_name", "name"), &m_class::set_prim_name); \
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "prim_name", \
        godot::PROPERTY_HINT_NONE, "" , \
        godot::PROPERTY_USAGE_STORAGE | godot::PROPERTY_USAGE_EDITOR | godot::PROPERTY_USAGE_READ_ONLY ), \
        "set_prim_name", "get_prim_name"); \
\
    godot::ClassDB::bind_method(godot::D_METHOD("get_prim_type"), &m_class::get_prim_type); \
    godot::ClassDB::bind_method(godot::D_METHOD("set_prim_type", "type"), &m_class::set_prim_type); \
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "prim_type", \
        godot::PROPERTY_HINT_NONE, "" , \
        godot::PROPERTY_USAGE_STORAGE | godot::PROPERTY_USAGE_EDITOR | godot::PROPERTY_USAGE_READ_ONLY ), \
        "set_prim_type", "get_prim_type"); \
\
    godot::ClassDB::bind_method(godot::D_METHOD("get_variantsets"), &m_class::get_variantsets); \
    godot::ClassDB::bind_method(godot::D_METHOD("set_variantsets", "variant_sets"), &m_class::set_variantsets); \
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::DICTIONARY, "variant_sets", \
        godot::PROPERTY_HINT_NONE, "" , \
        godot::PROPERTY_USAGE_STORAGE | godot::PROPERTY_USAGE_EDITOR | godot::PROPERTY_USAGE_READ_ONLY ), \
        "set_variantsets", "get_variantsets"); \
    \
    godot::ClassDB::bind_method(godot::D_METHOD("get_variantsets_selected_variants"), &m_class::get_variantsets_selected_variants); \
    godot::ClassDB::bind_method(godot::D_METHOD("set_variantsets_selected_variants", "variant_sets_values"), &m_class::set_variantsets_selected_variants); \
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::DICTIONARY, "variant_sets_values", \
        godot::PROPERTY_HINT_NONE, "" , \
        godot::PROPERTY_USAGE_STORAGE | godot::PROPERTY_USAGE_EDITOR ), \
        "set_variantsets_selected_variants", "get_variantsets_selected_variants");\
    \
    godot::ClassDB::bind_method(godot::D_METHOD("get_active_variant"), &m_class::get_active_variant); \
    godot::ClassDB::bind_method(godot::D_METHOD("set_active_variant", "value"), &m_class::set_active_variant); \
    ADD_PROPERTY(godot::PropertyInfo(godot::Variant::STRING, "active_variant", \
        godot::PROPERTY_HINT_NONE, ""), \
        "set_active_variant", "get_active_variant");\
    godot::ClassDB::bind_method(godot::D_METHOD("_apply_variant_change"), &m_class::_apply_variant_change);