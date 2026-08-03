//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./compute_Environment.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXCompute_Environment,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("Compute_Environment")
    // to find TfType<IDTXCompute_Environment>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, IDTXCompute_Environment>("Compute_Environment");
}

/* virtual */
IDTXCompute_Environment::~IDTXCompute_Environment()
{
}

/* static */
IDTXCompute_Environment
IDTXCompute_Environment::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_Environment();
    }
    return IDTXCompute_Environment(stage->GetPrimAtPath(path));
}

/* static */
IDTXCompute_Environment
IDTXCompute_Environment::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("Compute_Environment");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_Environment();
    }
    return IDTXCompute_Environment(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind IDTXCompute_Environment::_GetSchemaKind() const
{
    return IDTXCompute_Environment::schemaKind;
}

/* static */
const TfType &
IDTXCompute_Environment::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXCompute_Environment>();
    return tfType;
}

/* static */
bool 
IDTXCompute_Environment::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXCompute_Environment::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXCompute_Environment::GetInputsKeyAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->inputsKey);
}

UsdAttribute
IDTXCompute_Environment::CreateInputsKeyAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->inputsKey,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCompute_Environment::GetOutputsValueAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->outputsValue);
}

UsdAttribute
IDTXCompute_Environment::CreateOutputsValueAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->outputsValue,
                       SdfValueTypeNames->String,
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
IDTXCompute_Environment::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->inputsKey,
        IDTXTokens->outputsValue,
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
