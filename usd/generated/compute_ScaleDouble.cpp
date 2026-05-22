//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./compute_ScaleDouble.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXCompute_ScaleDouble,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("Compute_ScaleDouble")
    // to find TfType<IDTXCompute_ScaleDouble>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, IDTXCompute_ScaleDouble>("Compute_ScaleDouble");
}

/* virtual */
IDTXCompute_ScaleDouble::~IDTXCompute_ScaleDouble()
{
}

/* static */
IDTXCompute_ScaleDouble
IDTXCompute_ScaleDouble::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_ScaleDouble();
    }
    return IDTXCompute_ScaleDouble(stage->GetPrimAtPath(path));
}

/* static */
IDTXCompute_ScaleDouble
IDTXCompute_ScaleDouble::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("Compute_ScaleDouble");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_ScaleDouble();
    }
    return IDTXCompute_ScaleDouble(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind IDTXCompute_ScaleDouble::_GetSchemaKind() const
{
    return IDTXCompute_ScaleDouble::schemaKind;
}

/* static */
const TfType &
IDTXCompute_ScaleDouble::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXCompute_ScaleDouble>();
    return tfType;
}

/* static */
bool 
IDTXCompute_ScaleDouble::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXCompute_ScaleDouble::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXCompute_ScaleDouble::GetScalarFactorAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->scalarFactor);
}

UsdAttribute
IDTXCompute_ScaleDouble::CreateScalarFactorAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->scalarFactor,
                       SdfValueTypeNames->Double,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCompute_ScaleDouble::GetInputsValueAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->inputsValue);
}

UsdAttribute
IDTXCompute_ScaleDouble::CreateInputsValueAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->inputsValue,
                       SdfValueTypeNames->Double,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCompute_ScaleDouble::GetOutputsResultAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->outputsResult);
}

UsdAttribute
IDTXCompute_ScaleDouble::CreateOutputsResultAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->outputsResult,
                       SdfValueTypeNames->Double,
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
IDTXCompute_ScaleDouble::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->scalarFactor,
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
