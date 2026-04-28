//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./staticDataWidget.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXStaticDataWidget,
        TfType::Bases< UsdTyped > >();
    
    // Register the usd prim typename as an alias under UsdSchemaBase. This
    // enables one to call
    // TfType::Find<UsdSchemaBase>().FindDerivedByName("StaticDataWidget")
    // to find TfType<IDTXStaticDataWidget>, which is how IsA queries are
    // answered.
    TfType::AddAlias<UsdSchemaBase, IDTXStaticDataWidget>("StaticDataWidget");
}

/* virtual */
IDTXStaticDataWidget::~IDTXStaticDataWidget()
{
}

/* static */
IDTXStaticDataWidget
IDTXStaticDataWidget::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXStaticDataWidget();
    }
    return IDTXStaticDataWidget(stage->GetPrimAtPath(path));
}

/* static */
IDTXStaticDataWidget
IDTXStaticDataWidget::Define(
    const UsdStagePtr &stage, const SdfPath &path)
{
    static TfToken usdPrimTypeName("StaticDataWidget");
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXStaticDataWidget();
    }
    return IDTXStaticDataWidget(
        stage->DefinePrim(path, usdPrimTypeName));
}

/* virtual */
UsdSchemaKind IDTXStaticDataWidget::_GetSchemaKind() const
{
    return IDTXStaticDataWidget::schemaKind;
}

/* static */
const TfType &
IDTXStaticDataWidget::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXStaticDataWidget>();
    return tfType;
}

/* static */
bool 
IDTXStaticDataWidget::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXStaticDataWidget::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
IDTXStaticDataWidget::GetLabelNameAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->labelName);
}

UsdAttribute
IDTXStaticDataWidget::CreateLabelNameAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->labelName,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXStaticDataWidget::GetFixedValueAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->fixedValue);
}

UsdAttribute
IDTXStaticDataWidget::CreateFixedValueAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->fixedValue,
                       SdfValueTypeNames->String,
                       /* custom = */ false,
                       SdfVariabilityVarying,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
IDTXStaticDataWidget::GetColorModAttr() const
{
    return GetPrim().GetAttribute(IDTXTokens->colorMod);
}

UsdAttribute
IDTXStaticDataWidget::CreateColorModAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(IDTXTokens->colorMod,
                       SdfValueTypeNames->Color4f,
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
IDTXStaticDataWidget::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        IDTXTokens->labelName,
        IDTXTokens->fixedValue,
        IDTXTokens->colorMod,
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
