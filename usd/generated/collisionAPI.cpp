//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./collisionAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXCollisionAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
IDTXCollisionAPI::~IDTXCollisionAPI()
{
}

/* static */
IDTXCollisionAPI
IDTXCollisionAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCollisionAPI();
    }
    return IDTXCollisionAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind IDTXCollisionAPI::_GetSchemaKind() const
{
    return IDTXCollisionAPI::schemaKind;
}

/* static */
bool
IDTXCollisionAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<IDTXCollisionAPI>(whyNot);
}

/* static */
IDTXCollisionAPI
IDTXCollisionAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<IDTXCollisionAPI>()) {
        return IDTXCollisionAPI(prim);
    }
    return IDTXCollisionAPI();
}

/* static */
const TfType &
IDTXCollisionAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXCollisionAPI>();
    return tfType;
}

/* static */
bool 
IDTXCollisionAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXCollisionAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXCollisionAPI::GetCollisionShapeAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->collisionShape);
}

UsdAttribute
IDTXCollisionAPI::CreateCollisionShapeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->collisionShape,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCollisionAPI::GetCollisionTypeAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->collisionType);
}

UsdAttribute
IDTXCollisionAPI::CreateCollisionTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->collisionType,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCollisionAPI::GetCollisionInteractionTypesAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->collisionInteractionTypes);
}

UsdAttribute
IDTXCollisionAPI::CreateCollisionInteractionTypesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->collisionInteractionTypes,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

namespace {
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left,const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
}

/*static*/
const TfTokenVector&
IDTXCollisionAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->collisionShape,
        IDTXTokens->collisionType,
        IDTXTokens->collisionInteractionTypes,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdAPISchemaBase::GetSchemaAttributeNames(true),
            localNames);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--
PXR_NAMESPACE_OPEN_SCOPE

#include <pxr/base/tf/diagnostic.h>

VtArray<TfToken>
IDTXCollisionAPI::QueryInteractionTypesAttr(UsdPrim &prim) const
{
    VtArray<TfToken> result;

    UsdPrim parent = prim.GetParent();
    if (!parent) {
        return result;
    }

    const SdfPath& path = prim.GetPath();

    auto contains = [&](const TfToken& relName) {
        UsdRelationship rel = parent.GetRelationship(relName);
        SdfPathVector targets;
        rel.GetTargets(&targets);
        return std::find(targets.begin(), targets.end(), path) != targets.end();
    };

    if (contains(TfToken("physics:collider"))) {
        result.push_back(TfToken("Collide"));
    }

    if (contains(TfToken("physics:collider:query"))) {
        UsdAttribute attr =
            parent.GetAttribute(TfToken("interaction:interactionTypes"));

        VtArray<TfToken> types;
        if (attr && attr.Get(&types)) {
            
            
            TF_WARN("interactionTypes for %s:", parent.GetPath().GetText());
            OutputDebugStringA(parent.GetPath().GetText());
            for (const TfToken& t : types) {
                TF_WARN("  - %s", t.GetText());
            }

            result.insert(result.end(), types.begin(), types.end());
        }
    }

    // ensure unique tokens
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}



PXR_NAMESPACE_CLOSE_SCOPE
