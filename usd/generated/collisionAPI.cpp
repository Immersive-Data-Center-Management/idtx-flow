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
