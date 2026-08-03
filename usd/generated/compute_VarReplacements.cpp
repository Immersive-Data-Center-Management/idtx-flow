//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./compute_VarReplacements.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXCompute_VarReplacements,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("Compute_VarReplacements")
    // to find TfType<IDTXCompute_VarReplacements>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, IDTXCompute_VarReplacements>("Compute_VarReplacements");
}

/* virtual */
IDTXCompute_VarReplacements::~IDTXCompute_VarReplacements()
{
}

/* static */
IDTXCompute_VarReplacements
IDTXCompute_VarReplacements::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_VarReplacements();
    }
    return IDTXCompute_VarReplacements(stage->GetPrimAtPath(path));
}

/* static */
IDTXCompute_VarReplacements
IDTXCompute_VarReplacements::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("Compute_VarReplacements");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCompute_VarReplacements();
    }
    return IDTXCompute_VarReplacements(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind IDTXCompute_VarReplacements::_GetSchemaKind() const
{
    return IDTXCompute_VarReplacements::schemaKind;
}

/* static */
const TfType &
IDTXCompute_VarReplacements::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXCompute_VarReplacements>();
    return tfType;
}

/* static */
bool 
IDTXCompute_VarReplacements::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXCompute_VarReplacements::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXCompute_VarReplacements::GetInputsTemplateAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->inputsTemplate);
}

UsdAttribute
IDTXCompute_VarReplacements::CreateInputsTemplateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->inputsTemplate,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXCompute_VarReplacements::GetOutputsResultAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->outputsResult);
}

UsdAttribute
IDTXCompute_VarReplacements::CreateOutputsResultAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->outputsResult,
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
IDTXCompute_VarReplacements::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->inputsTemplate,
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
