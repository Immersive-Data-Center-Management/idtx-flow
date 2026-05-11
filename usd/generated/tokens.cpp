//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

IDTXTokensType::IDTXTokensType() :
    boundaries("boundaries", TfToken::Immortal),
    colors("colors", TfToken::Immortal),
    double_("double", TfToken::Immortal),
    float_("float", TfToken::Immortal),
    inputsInterval("inputs:interval", TfToken::Immortal),
    inputsJsonData("inputs:jsonData", TfToken::Immortal),
    inputsJsonPath("inputs:jsonPath", TfToken::Immortal),
    inputsJsonValueType("inputs:jsonValueType", TfToken::Immortal),
    inputsValue("inputs:value", TfToken::Immortal),
    none("none", TfToken::Immortal),
    outputsData("outputs:data", TfToken::Immortal),
    outputsJsonValueDouble("outputs:jsonValue:double", TfToken::Immortal),
    outputsJsonValueFloat("outputs:jsonValue:float", TfToken::Immortal),
    outputsJsonValueString("outputs:jsonValue:string", TfToken::Immortal),
    outputsResult("outputs:result", TfToken::Immortal),
    string("string", TfToken::Immortal),
    Compute_ColorFromFloat("Compute_ColorFromFloat", TfToken::Immortal),
    Compute_ValueFromJson("Compute_ValueFromJson", TfToken::Immortal),
    Datasource("Datasource", TfToken::Immortal),
    MockDatasource_RandomFloat("MockDatasource_RandomFloat", TfToken::Immortal),
    allTokens({
        boundaries,
        colors,
        double_,
        float_,
        inputsInterval,
        inputsJsonData,
        inputsJsonPath,
        inputsJsonValueType,
        inputsValue,
        none,
        outputsData,
        outputsJsonValueDouble,
        outputsJsonValueFloat,
        outputsJsonValueString,
        outputsResult,
        string,
        Compute_ColorFromFloat,
        Compute_ValueFromJson,
        Datasource,
        MockDatasource_RandomFloat
    })
{
}

TfStaticData<IDTXTokensType> IDTXTokens;

PXR_NAMESPACE_CLOSE_SCOPE
