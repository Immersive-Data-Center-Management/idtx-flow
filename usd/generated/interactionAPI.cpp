//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./interactionAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXInteractionAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
IDTXInteractionAPI::~IDTXInteractionAPI()
{
}

/* static */
IDTXInteractionAPI
IDTXInteractionAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXInteractionAPI();
    }
    return IDTXInteractionAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind IDTXInteractionAPI::_GetSchemaKind() const
{
    return IDTXInteractionAPI::schemaKind;
}

/* static */
bool
IDTXInteractionAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<IDTXInteractionAPI>(whyNot);
}

/* static */
IDTXInteractionAPI
IDTXInteractionAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<IDTXInteractionAPI>()) {
        return IDTXInteractionAPI(prim);
    }
    return IDTXInteractionAPI();
}

/* static */
const TfType &
IDTXInteractionAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXInteractionAPI>();
    return tfType;
}

/* static */
bool 
IDTXInteractionAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXInteractionAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXInteractionAPI::GetInteractionEnabledAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->interactionEnabled);
}

UsdAttribute
IDTXInteractionAPI::CreateInteractionEnabledAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->interactionEnabled,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXInteractionAPI::GetInteractionHighlightableAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->interactionHighlightable);
}

UsdAttribute
IDTXInteractionAPI::CreateInteractionHighlightableAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->interactionHighlightable,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXInteractionAPI::GetInteractionHighlightColorAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->interactionHighlightColor);
}

UsdAttribute
IDTXInteractionAPI::CreateInteractionHighlightColorAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->interactionHighlightColor,
                       SdfValueTypeNames->Color3f,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXInteractionAPI::GetInteractionIdentifierAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->interactionIdentifier);
}

UsdAttribute
IDTXInteractionAPI::CreateInteractionIdentifierAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->interactionIdentifier,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
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
IDTXInteractionAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->interactionEnabled,
        IDTXTokens->interactionHighlightable,
        IDTXTokens->interactionHighlightColor,
        IDTXTokens->interactionIdentifier,
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
