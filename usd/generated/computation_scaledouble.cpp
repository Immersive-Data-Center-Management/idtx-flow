/**
 * @file computation_scaledouble.cpp
 * @brief Implementation of the computation for prim type: IDTXCompute_ScaleDouble
 **/

#include <pxr/base/plug/registry.h>
#include <pxr/exec/exec/registerSchema.h>
#include <pxr/exec/vdf/context.h>

#include "./tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(_IDTXTokens,
    (connectedInputsValue)
    (resolvedValue)  // Convention: bridges prim computation output → attribute computation
);

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(IDTXCompute_ScaleDouble)
{
    self.PrimComputation(IDTXTokens->outputsResult)
        .Callback<double>(+[](const VdfContext& ctx)
        {
            // Get the scalar value
            double scalar = ctx.GetInputValue<double>(IDTXTokens->scalarFactor);            
            // Resolve the float input value 
            // GetInputValuePtr returns a pointer to the connected upstream
            // value when another prim's output is wired to inputs:value.
            // It returns nullptr when no connection is present.
            double value;
            if (const double* connectedPtr =
                    ctx.GetInputValuePtr<double>(_IDTXTokens->connectedInputsValue))
            {
                // A live USD attribute connection supplies the value.
                value = *connectedPtr;
            }
            else
            {
                // Fall back to the authored prim attribute value.
                value = ctx.GetInputValue<double>(IDTXTokens->inputsValue);
            }
            
            ctx.SetOutput<double>(scalar * value);
        }).Inputs(
            // Declare inputs:value as an attribute input that also follows
            // any USD attribute connection authored on it.
            // .Connections<T>(bridgeToken)  instructs OpenExec to evaluate
            //     the computation registered under "bridgeToken" on the
            //     connected source prim and deliver the result.
            // .InputName(localToken)  gives the resolved connection value
            //     a distinct name so GetInputValuePtr can distinguish it
            //     from the plain authored attribute value.
            Attribute(IDTXTokens->inputsValue)
                .Connections<double>(_IDTXTokens->resolvedValue)
                .InputName(_IDTXTokens->connectedInputsValue),
            // Also request the raw authored attribute value as a fallback.
            AttributeValue<double>(IDTXTokens->inputsValue),
            // The scalarFactor input
            AttributeValue<double>(IDTXTokens->scalarFactor).Required()
        );
        
    self.AttributeComputation(IDTXTokens->outputsResult, _IDTXTokens->resolvedValue)
        .Callback<double>(+[](const VdfContext& ctx)
        {
            ctx.SetOutput<double>(
                ctx.GetInputValue<double>(IDTXTokens->outputsResult)
            );
        }).Inputs(
            Prim().Computation<double>(IDTXTokens->outputsResult)
        );
}