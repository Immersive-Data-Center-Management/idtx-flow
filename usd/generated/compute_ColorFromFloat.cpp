//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./compute_ColorFromFloat.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXCompute_ColorFromFloat,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("Compute_ColorFromFloat")
    // to find TfType<IDTXCompute_ColorFromFloat>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, IDTXCompute_ColorFromFloat>("Compute_ColorFromFloat");
}

/* virtual */
IDTXCompute_ColorFromFloat::~IDTXCompute_ColorFromFloat()
{
}

/* static */
IDTXCompute_ColorFromFloat
IDTXCompute_ColorFromFloat::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_ColorFromFloat();
    }
    return IDTXCompute_ColorFromFloat(stage->GetPrimAtPath(path));
}

/* static */
IDTXCompute_ColorFromFloat
IDTXCompute_ColorFromFloat::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("Compute_ColorFromFloat");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_ColorFromFloat();
    }
    return IDTXCompute_ColorFromFloat(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind IDTXCompute_ColorFromFloat::_GetSchemaKind() const
{
    return IDTXCompute_ColorFromFloat::schemaKind;
}

/* static */
const TfType &
IDTXCompute_ColorFromFloat::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXCompute_ColorFromFloat>();
    return tfType;
}

/* static */
bool 
IDTXCompute_ColorFromFloat::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXCompute_ColorFromFloat::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXCompute_ColorFromFloat::GetBoundariesAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->boundaries);
}

UsdAttribute
IDTXCompute_ColorFromFloat::CreateBoundariesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->boundaries,
                       SdfValueTypeNames->FloatArray,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCompute_ColorFromFloat::GetColorsAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->colors);
}

UsdAttribute
IDTXCompute_ColorFromFloat::CreateColorsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->colors,
                       SdfValueTypeNames->Color3fArray,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCompute_ColorFromFloat::GetInputsValueAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->inputsValue);
}

UsdAttribute
IDTXCompute_ColorFromFloat::CreateInputsValueAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->inputsValue,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCompute_ColorFromFloat::GetOutputsResultAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->outputsResult);
}

UsdAttribute
IDTXCompute_ColorFromFloat::CreateOutputsResultAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->outputsResult,
                       SdfValueTypeNames->Color3f,
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
IDTXCompute_ColorFromFloat::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->boundaries,
        IDTXTokens->colors,
        IDTXTokens->inputsValue,
        IDTXTokens->outputsResult,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdTyped::GetSchemaAttributeNames(true),
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
