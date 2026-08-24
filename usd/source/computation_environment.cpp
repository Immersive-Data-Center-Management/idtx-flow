/**
 * @file computation_environment.cpp
 * @brief Implementation of the computation registered along the IDTXCompute_Environment schema.
 *
 * The Compute_Environment node resolves its string input `inputs:key` into the string
 * output `outputs:value` by delegating to a host-registered provider via the
 * EnvironmentProviderRegistry. The USD library itself has no knowledge of *how* the value
 * is produced (environment variable, engine project setting, etc.) - that is entirely
 * up to the provider(s) the host registers at runtime.
 *
 * The input key may carry a routing prefix understood by the registry, e.g.
 *   "env:PATH"                -> environment variable PATH
 *   "project:physics/gravity" -> engine project setting physics/gravity
 * Keys without a recognized prefix are routed to the default provider (if any).
 *
 * If no matching provider is registered (or the key cannot be resolved) the output is an
 * empty string. This mirrors the graceful-degradation behavior of the other IDTX compute
 * nodes and never crashes when a stage is loaded without a host that registers providers.
 **/

#include <string>

#include <pxr/base/plug/registry.h>
#include <pxr/exec/exec/registerSchema.h>
#include <pxr/exec/exec/builtinComputations.h>
#include <pxr/exec/vdf/context.h>
#include <pxr/exec/ef/time.h>

#include <tokens.h>
#include <EnvironmentProvider.h>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(_IDTXTokens,
    (connectedInputsKey)
    (resolvedValue)  // Convention: bridges prim computation output → attribute computation
);

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(IDTXCompute_Environment)
{
    self.PrimComputation(IDTXTokens->outputsValue)
        .Callback<std::string>(+[](const VdfContext& ctx)
        {
            // Resolve the input key. As with the other compute nodes, prefer a live USD
            // attribute connection when present, otherwise fall back to the authored value.
            std::string key;
            if (const std::string* connectedPtr =
                    ctx.GetInputValuePtr<std::string>(_IDTXTokens->connectedInputsKey))
            {
                key = *connectedPtr;
            }
            else
            {
                key = ctx.GetInputValue<std::string>(IDTXTokens->inputsKey);
            }

            // An empty key can never resolve to anything meaningful.
            if (key.empty())
            {
                ctx.SetOutput<std::string>(std::string());
                return;
            }

            // Delegate the actual lookup to the host-registered provider. If nothing is
            // registered, or the key cannot be resolved, an empty string is returned.
            const std::string value =
                idtx::EnvironmentProviderRegistry::Instance().Resolve(key, std::string());

            ctx.SetOutput<std::string>(value);
        })
        .Inputs(
            // Declare inputs:key as an attribute input that also follows any USD attribute
            // connection authored on it (so it can be driven by another compute node's
            // string output), while still exposing the raw authored value as a fallback.
            Attribute(IDTXTokens->inputsKey)
                .Connections<std::string>(_IDTXTokens->resolvedValue)
                .InputName(_IDTXTokens->connectedInputsKey),
            AttributeValue<std::string>(IDTXTokens->inputsKey),
            // Declare a dependency on the stage's builtin time computation. This is what makes
            // THIS computation time-dependent (opt-in, per computation): whenever the host
            // advances the Exec time via ExecUsdSystem::ChangeTime(), Exec re-resolves this
            // input, sees it changed, and invalidates + recomputes the callback above. That in
            // turn re-runs the environment retreivel so volatile tokens like ${auth:TOKEN} pick up
            // their fresh values. The EfTime value itself is not used by the callback; the input
            // exists solely to hook this node into Exec's time-based invalidation.
            Stage().Computation<EfTime>(ExecBuiltinComputations->computeTime)
        );

    // Prim-to-attribute glue: makes the prim's computed output reachable as a USD attribute
    // connection target so downstream prims can author
    //     string someAttr.connect = </Scene/MyEnvNode.outputs:value>
    self.AttributeComputation(IDTXTokens->outputsValue, _IDTXTokens->resolvedValue)
        .Callback<std::string>(+[](const VdfContext& ctx)
        {
            ctx.SetOutput<std::string>(
                ctx.GetInputValue<std::string>(IDTXTokens->outputsValue)
            );
        })
        .Inputs(
            Prim().Computation<std::string>(IDTXTokens->outputsValue)
        );
}