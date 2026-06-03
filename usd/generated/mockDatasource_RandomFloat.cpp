//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./mockDatasource_RandomFloat.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXMockDatasource_RandomFloat,
        TfType::Bases< IDTXDatasource > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("MockDatasource_RandomFloat")
    // to find TfType<IDTXMockDatasource_RandomFloat>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, IDTXMockDatasource_RandomFloat>("MockDatasource_RandomFloat");
}

/* virtual */
IDTXMockDatasource_RandomFloat::~IDTXMockDatasource_RandomFloat()
{
}

/* static */
IDTXMockDatasource_RandomFloat
IDTXMockDatasource_RandomFloat::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXMockDatasource_RandomFloat();
    }
    return IDTXMockDatasource_RandomFloat(stage->GetPrimAtPath(path));
}

/* static */
IDTXMockDatasource_RandomFloat
IDTXMockDatasource_RandomFloat::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("MockDatasource_RandomFloat");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXMockDatasource_RandomFloat();
    }
    return IDTXMockDatasource_RandomFloat(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind IDTXMockDatasource_RandomFloat::_GetSchemaKind() const
{
    return IDTXMockDatasource_RandomFloat::schemaKind;
}

/* static */
const TfType &
IDTXMockDatasource_RandomFloat::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXMockDatasource_RandomFloat>();
    return tfType;
}

/* static */
bool 
IDTXMockDatasource_RandomFloat::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXMockDatasource_RandomFloat::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXMockDatasource_RandomFloat::GetIntervalAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->interval);
}

UsdAttribute
IDTXMockDatasource_RandomFloat::CreateIntervalAttr(VtValue const &defaultValue, bool writeSparsely) const
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
IDTXMockDatasource_RandomFloat::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
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
