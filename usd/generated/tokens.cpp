//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

IDTXTokensType::IDTXTokensType() :
    double_("double", TfToken::Immortal),
    float_("float", TfToken::Immortal),
    inputsInterval("inputs:interval", TfToken::Immortal),
    inputsJsonData("inputs:jsonData", TfToken::Immortal),
    inputsJsonPath("inputs:jsonPath", TfToken::Immortal),
    inputsJsonValueType("inputs:jsonValueType", TfToken::Immortal),
    none("none", TfToken::Immortal),
    outputsData("outputs:data", TfToken::Immortal),
    outputsJsonValueDouble("outputs:jsonValue:double", TfToken::Immortal),
    outputsJsonValueFloat("outputs:jsonValue:float", TfToken::Immortal),
    outputsJsonValueString("outputs:jsonValue:string", TfToken::Immortal),
    string("string", TfToken::Immortal),
    Compute_ValueFromJson("Compute_ValueFromJson", TfToken::Immortal),
    Datasource("Datasource", TfToken::Immortal),
    MockDatasource_RandomFloat("MockDatasource_RandomFloat", TfToken::Immortal),
    allTokens({
        double_,
        float_,
        inputsInterval,
        inputsJsonData,
        inputsJsonPath,
        inputsJsonValueType,
        none,
        outputsData,
        outputsJsonValueDouble,
        outputsJsonValueFloat,
        outputsJsonValueString,
        string,
        Compute_ValueFromJson,
        Datasource,
        MockDatasource_RandomFloat
    })
{
}

TfStaticData<IDTXTokensType> IDTXTokens;

PXR_NAMESPACE_CLOSE_SCOPE
