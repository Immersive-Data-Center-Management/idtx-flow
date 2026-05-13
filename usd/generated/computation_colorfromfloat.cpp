/**
 * @file computation_colorfromfloat.cpp
 * @brief 
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
#include <pxr/base/gf/vec3f.h>

#include "./tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(_IDTXTokens,
    (connectedInputsValue)
    (resolvedValue)  // Convention: bridges prim computation output → attribute computation
    ((resultBaseName, "result"))
);

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(IDTXCompute_ColorFromFloat)
{
    self.PrimComputation(IDTXTokens->outputsResult)
        .Callback<GfVec3f>(+[](const VdfContext& ctx) {
            std::cout << "Prim Computation for IDTXCompute_ColorFromFloat" << std::endl;
            float value;
            if (const float* connected_value_ptr = ctx.GetInputValuePtr<float>(_IDTXTokens->connectedInputsValue))
            {
                value = *connected_value_ptr;
            } else
            {
                // use authored value as fall back to connected value
                value = ctx.GetInputValue<float>(IDTXTokens->inputsValue);
            }
            // arrays are not yet supported
            //const VtArray<GfVec3f>& colors = ctx.GetInputValue<VtArray<GfVec3f>>(IDTXTokens->colors);
            //const VtArray<float>& boundaries = ctx.GetInputValue<VtArray<float>>(IDTXTokens->boundaries);
            // Temperature-to-color mapping
            VtArray<float> boundaries = {15, 25, 35};
            VtArray<GfVec3f> colors = {{0, 0, 1}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
            // Validate
            if (colors.size() != boundaries.size() + 1) {
                TF_RUNTIME_ERROR("colors array size must be boundaries size + 1");
                return;
            }

            // Binary search for correct bucket
            size_t bucket = 0;
            for (size_t i = 0; i < boundaries.size(); ++i) {
                if (value >= boundaries[i]) {
                    bucket = i + 1;
                } else {
                    break;
                }
            }
            ctx.SetOutput<GfVec3f>(colors[bucket]);
        })
        .Inputs(
            Attribute(IDTXTokens->inputsValue)
                .Connections<float>(_IDTXTokens->resolvedValue) // follow connections and call the target computation
                .InputName(_IDTXTokens->connectedInputsValue),
            AttributeValue<float>(IDTXTokens->inputsValue) // get authored value
        );
    
    self.AttributeComputation(IDTXTokens->outputsResult, _IDTXTokens->resolvedValue)
        .Callback<GfVec3f>(+[](const VdfContext& ctx)
        {
            std::cout << "Attribute Computation for IDTXCompute_ColorFromFloat that bridges to PrimComputation" << std::endl;
            ctx.SetOutput<GfVec3f>(
                ctx.GetInputValue<GfVec3f>(IDTXTokens->outputsResult)
            );
        })
        .Inputs(
            Prim().Computation<GfVec3f>(IDTXTokens->outputsResult)
        );
}