//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef IDTX_TOKENS_H
#define IDTX_TOKENS_H

/// \file IDTX/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class IDTXTokensType
///
/// \link IDTXTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// IDTXTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use IDTXTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(IDTXTokens->boundaries);
/// \endcode
struct IDTXTokensType {
    IDTX_API IDTXTokensType();
    /// \brief "boundaries"
    /// 
    /// IDTXCompute_ColorFromFloat
    const TfToken boundaries;
    /// \brief "colors"
    /// 
    /// IDTXCompute_ColorFromFloat
    const TfToken colors;
    /// \brief "double"
    /// 
    /// Possible value for IDTXCompute_ValueFromJson::GetInputsJsonValueTypeAttr()
    const TfToken double_;
    /// \brief "float"
    /// 
    /// Possible value for IDTXCompute_ValueFromJson::GetInputsJsonValueTypeAttr()
    const TfToken float_;
    /// \brief "inputs:interval"
    /// 
    /// IDTXMockDatasource_RandomFloat
    const TfToken inputsInterval;
    /// \brief "inputs:jsonData"
    /// 
    /// IDTXCompute_ValueFromJson
    const TfToken inputsJsonData;
    /// \brief "inputs:jsonPath"
    /// 
    /// IDTXCompute_ValueFromJson
    const TfToken inputsJsonPath;
    /// \brief "inputs:jsonValueType"
    /// 
    /// IDTXCompute_ValueFromJson
    const TfToken inputsJsonValueType;
    /// \brief "inputs:value"
    /// 
    /// IDTXCompute_ColorFromFloat
    const TfToken inputsValue;
    /// \brief "none"
    /// 
    /// Possible value for IDTXCompute_ValueFromJson::GetInputsJsonValueTypeAttr()
    const TfToken none;
    /// \brief "outputs:data"
    /// 
    /// IDTXDatasource
    const TfToken outputsData;
    /// \brief "outputs:jsonValue:double"
    /// 
    /// IDTXCompute_ValueFromJson
    const TfToken outputsJsonValueDouble;
    /// \brief "outputs:jsonValue:float"
    /// 
    /// IDTXCompute_ValueFromJson
    const TfToken outputsJsonValueFloat;
    /// \brief "outputs:jsonValue:string"
    /// 
    /// IDTXCompute_ValueFromJson
    const TfToken outputsJsonValueString;
    /// \brief "outputs:result"
    /// 
    /// IDTXCompute_ColorFromFloat
    const TfToken outputsResult;
    /// \brief "string"
    /// 
    /// Possible value for IDTXCompute_ValueFromJson::GetInputsJsonValueTypeAttr()
    const TfToken string;
    /// \brief "Compute_ColorFromFloat"
    /// 
    /// Schema identifer and family for IDTXCompute_ColorFromFloat
    const TfToken Compute_ColorFromFloat;
    /// \brief "Compute_ValueFromJson"
    /// 
    /// Schema identifer and family for IDTXCompute_ValueFromJson
    const TfToken Compute_ValueFromJson;
    /// \brief "Datasource"
    /// 
    /// Schema identifer and family for IDTXDatasource
    const TfToken Datasource;
    /// \brief "MockDatasource_RandomFloat"
    /// 
    /// Schema identifer and family for IDTXMockDatasource_RandomFloat
    const TfToken MockDatasource_RandomFloat;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var IDTXTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa IDTXTokensType
extern IDTX_API TfStaticData<IDTXTokensType> IDTXTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
