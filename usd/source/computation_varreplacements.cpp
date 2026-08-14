/**
 * @file computation_varreplacements.cpp
 * @brief Implementation of the computation registered along the IDTXCompute_VarReplacements schema.
 *
 * The Compute_VarReplacements node takes its string input `inputs:template` and replaces
 * every token of the form ${<type>:<name>} with a value resolved by a host-registered
 * provider via the EnvironmentProviderRegistry. The result is written to `outputs:result`.
 *
 * The <type> part selects the provider domain (e.g. "env", "auth", "var") and the <name>
 * part is the key passed to that provider. This reuses the exact same routing that the
 * Compute_Environment node uses: the registry splits a "<type>:<name>" key on the first
 * colon and dispatches to the provider registered for that prefix.
 *
 * Examples of tokens understood by this node (given matching registered providers):
 *   "${env:HOME}"                -> environment variable HOME
 *   "${var:CURRENT_TIME}"        -> the current UTC timestamp
 *   "${var:CURRENT_TIME_LESS5}"  -> the current UTC timestamp minus 5 seconds
 *   "${auth:TOKEN}"              -> an authentication token
 *
 * Tokens that cannot be resolved (unknown provider, unresolvable name, or malformed
 * token) are replaced with an empty string. This mirrors the graceful-degradation
 * behavior of the other IDTX compute nodes and never crashes when a stage is loaded
 * without a host that registers providers.
 *
 * ## Time dependency (why and how)
 *
 * OpenExec caches computed values and only recomputes a node when one of the inputs it
 * *knows about* is invalidated. The tokens resolved here, however, depend on values that
 * live entirely outside the compute graph (e.g. "${var:CURRENT_TIME}", an auth token that
 * rotates, etc.). Their string *template* input never changes, so from Exec's point of view
 * there is no reason to ever recompute — it would happily serve the first cached result
 * forever.
 *
 * To make Exec re-run this computation we declare an explicit dependency on the stage's
 * builtin `computeTime` computation (an EfTime value). When the host advances the Exec
 * evaluation time (see ExecUsdSystem::ChangeTime in the ExecBridge worker), Exec detects
 * that this node's time-dependent input changed and invalidates + recomputes it — through
 * its *proper* invalidation path, so downstream nodes and the result cache stay consistent.
 *
 * IMPORTANT: This time dependency is a *per-computation* property, expressed purely by adding
 * the `Stage().Computation<EfTime>(...)` input below. It is NOT a fixed property of the schema
 * and it is NOT global to all IDTX compute nodes: e.g. Compute_Environment deliberately does
 * *not* declare it and therefore keeps benefiting from Exec's caching. If a future computation
 * needs to be volatile, it opts in the same way; if it should stay cached, it simply omits the
 * input. The dependency is thus configurable at registration time, one computation at a time.
 **/

#include <string>

#include <pxr/base/plug/registry.h>
#include <pxr/exec/ef/time.h>
#include <pxr/exec/exec/builtinComputations.h>
#include <pxr/exec/exec/registerSchema.h>
#include <pxr/exec/vdf/context.h>

#include <tokens.h>
#include <EnvironmentProvider.h>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(_IDTXTokens,
    (connectedInputsTemplate)
    (resolvedValue)  // Convention: bridges prim computation output → attribute computation
);

/// Replace every token of the form ${<type>:<name>} inside @p input with the value
/// resolved by the host-registered provider for <type>. Unresolvable or malformed
/// tokens are replaced with an empty string. Any '$' not starting a well-formed
/// "${...}" sequence is copied through verbatim.
static std::string _ReplaceVars(const std::string& input)
{
    std::string result;
    result.reserve(input.size());

    std::string::size_type pos = 0;
    while (pos < input.size())
    {
        const std::string::size_type start = input.find("${", pos);
        if (start == std::string::npos)
        {
            // No more tokens; copy the remainder verbatim.
            result.append(input, pos, std::string::npos);
            break;
        }

        // Copy everything up to the token start.
        result.append(input, pos, start - pos);

        const std::string::size_type end = input.find('}', start + 2);
        if (end == std::string::npos)
        {
            // Unterminated token; copy the rest verbatim and stop.
            result.append(input, start, std::string::npos);
            break;
        }

        // Extract the inner "<type>:<name>" key and route it through the registry.
        const std::string key = input.substr(start + 2, end - (start + 2));
        result.append(idtx::EnvironmentProviderRegistry::Instance().Resolve(key, std::string()));

        pos = end + 1;
    }

    return result;
}

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(IDTXCompute_VarReplacements)
{
    self.PrimComputation(IDTXTokens->outputsResult)
        .Callback<std::string>(+[](const VdfContext& ctx)
        {
            // Resolve the input template. As with the other compute nodes, prefer a live
            // USD attribute connection when present, otherwise fall back to the authored value.
            std::string templateStr;
            if (const std::string* connectedPtr =
                    ctx.GetInputValuePtr<std::string>(_IDTXTokens->connectedInputsTemplate))
            {
                templateStr = *connectedPtr;
            }
            else
            {
                templateStr = ctx.GetInputValue<std::string>(IDTXTokens->inputsTemplate);
            }

            // An empty template can never contain a token to replace.
            if (templateStr.empty())
            {
                ctx.SetOutput<std::string>(std::string());
                return;
            }

            ctx.SetOutput<std::string>(_ReplaceVars(templateStr));
        })
        .Inputs(
            // Declare inputs:template as an attribute input that also follows any USD attribute
            // connection authored on it (so it can be driven by another compute node's string
            // output), while still exposing the raw authored value as a fallback.
            Attribute(IDTXTokens->inputsTemplate)
                .Connections<std::string>(_IDTXTokens->resolvedValue)
                .InputName(_IDTXTokens->connectedInputsTemplate),
            AttributeValue<std::string>(IDTXTokens->inputsTemplate),
            // Declare a dependency on the stage's builtin time computation. This is what makes
            // THIS computation time-dependent (opt-in, per computation): whenever the host
            // advances the Exec time via ExecUsdSystem::ChangeTime(), Exec re-resolves this
            // input, sees it changed, and invalidates + recomputes the callback above. That in
            // turn re-runs _ReplaceVars() so volatile tokens like ${var:CURRENT_TIME} pick up
            // their fresh values. The EfTime value itself is not used by the callback; the input
            // exists solely to hook this node into Exec's time-based invalidation.
            Stage().Computation<EfTime>(ExecBuiltinComputations->computeTime)
        );

    // Prim-to-attribute glue: makes the prim's computed output reachable as a USD attribute
    // connection target so downstream prims can author
    //     string someAttr.connect = </Scene/MyVarNode.outputs:result>
    self.AttributeComputation(IDTXTokens->outputsResult, _IDTXTokens->resolvedValue)
        .Callback<std::string>(+[](const VdfContext& ctx)
        {
            ctx.SetOutput<std::string>(
                ctx.GetInputValue<std::string>(IDTXTokens->outputsResult)
            );
        })
        .Inputs(
            Prim().Computation<std::string>(IDTXTokens->outputsResult)
        );
}