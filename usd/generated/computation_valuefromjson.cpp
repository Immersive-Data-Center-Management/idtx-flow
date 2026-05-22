/**
 * @file computation_valuefromjson.cpp
 * @brief Implementation of the computation thet will be registered along the IDTXCompute_ValueFromJson schema
 *
 **/

#include <iostream>
#include <string>
#include <sstream>

#include <pxr/base/plug/registry.h>
#include <pxr/exec/exec/registerSchema.h>
#include <pxr/exec/vdf/context.h>
#include <pxr/base/js/json.h>
#include <pxr/base/js/value.h>

#include "./tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(_IDTXTokens,
    (resolvedValue)  // Convention: bridges prim computation output → attribute computation
    ((jsonValueFloatBaseName, "float"))
    ((jsonValueDoubleBaseName, "double"))
    ((jsonValueStringBaseName, "string"))
);

/// Resolve a JSON Pointer (RFC 6901) path against a parsed JsValue tree.
///
/// Path format uses '/' as separator; array indices are bare integers:
///   "/data/temperature"         -> root["data"]["temperature"]
///   "/data/sensors/0/temperature" -> root["data"]["sensors"][0]["temperature"]
///   "/values/2"                 -> root["values"][2]
///
/// A leading '/' is optional and will be skipped.
/// Returns a null JsValue if any traversal step fails.
///
static JsValue _ResolveJsonPointer(const JsValue& root, const std::string& pointer) {
    JsValue current = root;
    std::istringstream stream(pointer);
    std::string token;

    while (std::getline(stream, token, '/')) {
        if (token.empty()) {
            continue; // skip leading or double slashes
        }

        if (current.IsObject()) {
            const JsObject& obj = current.GetJsObject();
            const auto it = obj.find(token);
            if (it == obj.end()) {
                std::cerr << "JSON Pointer error: key '" << token
                          << "' not found in object" << std::endl;
                return JsValue();
            }
            current = it->second;
        } else if (current.IsArray()) {
            size_t idx;
            try {
                idx = std::stoull(token);
            } catch (const std::exception&) {
                std::cerr << "JSON Pointer error: expected array index, got '"
                          << token << "'" << std::endl;
                return JsValue();
            }
            const JsArray& arr = current.GetJsArray();
            if (idx >= arr.size()) {
                std::cerr << "JSON Pointer error: index " << idx
                          << " out of bounds (size=" << arr.size() << ")" << std::endl;
                return JsValue();
            }
            current = arr[idx];
        } else {
            std::cerr << "JSON Pointer error: cannot traverse into "
                      << current.GetTypeName() << " with token '" << token
                      << "'" << std::endl;
            return JsValue();
        }
    }

    return current;
}

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(IDTXCompute_ValueFromJson)
{
    self.PrimComputation(IDTXTokens->outputsJsonValueFloat)
        .Callback<float>(+[](const VdfContext &ctx) {
            const std::string* jsonDataPtr = ctx.GetInputValuePtr<std::string>(IDTXTokens->inputsJsonData);
            if (!jsonDataPtr)
            {
                ctx.SetOutput<float>(0.0);
                return;
            }
            const std::string& jsonPointer = ctx.GetInputValue<std::string>(IDTXTokens->jsonPath);

            // Parse the JSON string
            JsParseError parseError;
            const JsValue root = JsParseString(*jsonDataPtr, &parseError);
            if (!root) {
                ctx.SetOutput<float>(0.0);
                return;
            }

            // Resolve the RFC 6901 JSON Pointer into the tree
            const JsValue resolved = _ResolveJsonPointer(root, jsonPointer);
            if (!resolved) {
                ctx.SetOutput<float>(0.0);
                return;
            }

            // Extract the numeric value
            if (resolved.IsReal()) {
                ctx.SetOutput<float>(static_cast<float>(resolved.GetReal()));
            } else if (resolved.IsInt()) {
                ctx.SetOutput<float>(static_cast<float>(resolved.GetInt()));
            } else {
                ctx.SetOutput<float>(0.0);
            }
        })
        .Inputs(
            Attribute(IDTXTokens->inputsJsonData)
                .Connections<std::string>(ExecBuiltinComputations->computeValue)
                .InputName(IDTXTokens->inputsJsonData),
            AttributeValue<std::string>(IDTXTokens->jsonPath).Required()
        );
    
    // Register the computed value as computation to make it accessible via ConnectionTargetedObjects
    self.AttributeComputation(IDTXTokens->outputsJsonValueFloat, _IDTXTokens->resolvedValue)
        .Callback<float>(+[](const VdfContext &ctx)
        {
            ctx.SetOutput<float>(
                ctx.GetInputValue<float>(IDTXTokens->outputsJsonValueFloat)
            );
        })
        .Inputs(
            Prim()
                .Computation<float>(IDTXTokens->outputsJsonValueFloat)
        );
    
    self.PrimComputation(IDTXTokens->outputsJsonValueDouble)
        .Callback<double>(+[](const VdfContext &ctx) {
            const std::string* jsonDataPtr = ctx.GetInputValuePtr<std::string>(IDTXTokens->inputsJsonData);
            if (!jsonDataPtr)
            {
                ctx.SetOutput<double>(0.0);
                return;
            }
            const std::string& jsonPointer = ctx.GetInputValue<std::string>(IDTXTokens->jsonPath);

            // Parse the JSON string
            JsParseError parseError;
            JsValue root = JsParseString(*jsonDataPtr, &parseError);
            if (!root) {
                ctx.SetOutput<double>(0.0);
                return;
            }

            // Resolve the RFC 6901 JSON Pointer into the tree
            JsValue resolved = _ResolveJsonPointer(root, jsonPointer);
            if (!resolved) {
                ctx.SetOutput<double>(0.0);
                return;
            }

            // Extract the numeric value
            if (resolved.IsReal()) {
                ctx.SetOutput<double>(resolved.GetReal());
            } else if (resolved.IsInt()) {
                ctx.SetOutput<double>(static_cast<double>(resolved.GetInt()));
            } else {
                ctx.SetOutput<double>(0.0);
            }
        })
        .Inputs(
            Attribute(IDTXTokens->inputsJsonData)
                .Connections<std::string>(ExecBuiltinComputations->computeValue)
                .InputName(IDTXTokens->inputsJsonData),
            AttributeValue<std::string>(IDTXTokens->jsonPath)
                .Required()
        );
    
    // Register the computed value as computation to make it accessible via ConnectionTargetedObjects
    self.AttributeComputation(IDTXTokens->outputsJsonValueDouble, _IDTXTokens->resolvedValue)
        .Callback<double>(+[](const VdfContext &ctx)
        {
            ctx.SetOutput<double>(
                ctx.GetInputValue<double>(IDTXTokens->outputsJsonValueDouble)
            );
        })
        .Inputs(
            Prim().Computation<double>(IDTXTokens->outputsJsonValueDouble)
        );
    
    self.PrimComputation(IDTXTokens->outputsJsonValueString)
        .Callback<std::string>(+[](const VdfContext &ctx) {
            const std::string* jsonDataPtr = ctx.GetInputValuePtr<std::string>(IDTXTokens->inputsJsonData);
            if (!jsonDataPtr)
            {
                ctx.SetOutput<std::string>("");
                return;
            }
            const std::string& jsonPointer = ctx.GetInputValue<std::string>(IDTXTokens->jsonPath);

            // Parse the JSON string
            JsParseError parseError;
            JsValue root = JsParseString(*jsonDataPtr, &parseError);
            if (!root) {
                ctx.SetOutput<std::string>("");
                return;
            }

            // Resolve the RFC 6901 JSON Pointer into the tree
            JsValue resolved = _ResolveJsonPointer(root, jsonPointer);
            if (!resolved) {
                ctx.SetOutput<std::string>("");
                return;
            }

            // Extract the numeric value
            if (resolved.IsString()) {
                ctx.SetOutput<std::string>(resolved.GetString());
            } else {
                ctx.SetOutput<std::string>("");
            }
        })
        .Inputs(
            Attribute(IDTXTokens->inputsJsonData)
                .Connections<std::string>(ExecBuiltinComputations->computeValue)
                .InputName(IDTXTokens->inputsJsonData),
            AttributeValue<std::string>(IDTXTokens->jsonPath)
                .Required()
        );
    
    // Register the computed value as computation to make it accessible via ConnectionTargetedObjects
    self.AttributeComputation(IDTXTokens->outputsJsonValueString, _IDTXTokens->resolvedValue)
        .Callback<std::string>(+[](const VdfContext &ctx)
        {
            ctx.SetOutput<std::string>(
                ctx.GetInputValue<std::string>(IDTXTokens->outputsJsonValueString)
            );
        })
        .Inputs(
            Prim().Computation<std::string>(IDTXTokens->outputsJsonValueString)
        );
}