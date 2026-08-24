//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./restDatasource.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXRestDatasource,
        TfType::Bases< IDTXDatasource > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("RestDatasource")
    // to find TfType<IDTXRestDatasource>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, IDTXRestDatasource>("RestDatasource");
}

/* virtual */
IDTXRestDatasource::~IDTXRestDatasource()
{
}

/* static */
IDTXRestDatasource
IDTXRestDatasource::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXRestDatasource();
    }
    return IDTXRestDatasource(stage->GetPrimAtPath(path));
}

/* static */
IDTXRestDatasource
IDTXRestDatasource::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("RestDatasource");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXRestDatasource();
    }
    return IDTXRestDatasource(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind IDTXRestDatasource::_GetSchemaKind() const
{
    return IDTXRestDatasource::schemaKind;
}

/* static */
const TfType &
IDTXRestDatasource::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXRestDatasource>();
    return tfType;
}

/* static */
bool 
IDTXRestDatasource::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXRestDatasource::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXRestDatasource::GetEndpointAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->endpoint);
}

UsdAttribute
IDTXRestDatasource::CreateEndpointAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->endpoint,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXRestDatasource::GetQueryAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->query);
}

UsdAttribute
IDTXRestDatasource::CreateQueryAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->query,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXRestDatasource::GetMethodAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->method);
}

UsdAttribute
IDTXRestDatasource::CreateMethodAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->method,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXRestDatasource::GetAuthorizationAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->authorization);
}

UsdAttribute
IDTXRestDatasource::CreateAuthorizationAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->authorization,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXRestDatasource::GetJsonBodyAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->jsonBody);
}

UsdAttribute
IDTXRestDatasource::CreateJsonBodyAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->jsonBody,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXRestDatasource::GetIntervalAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->interval);
}

UsdAttribute
IDTXRestDatasource::CreateIntervalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->interval,
                       SdfValueTypeNames->Float,
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
IDTXRestDatasource::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->endpoint,
        IDTXTokens->query,
        IDTXTokens->method,
        IDTXTokens->authorization,
        IDTXTokens->jsonBody,
        IDTXTokens->interval,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            IDTXDatasource::GetSchemaAttributeNames(true),
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
