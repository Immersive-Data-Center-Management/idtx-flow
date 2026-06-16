# IDTXFlow Data Sources and Data Flow

## Overview

IDTXFlow extends the standard OpenUSD prim conversion pipeline with a **live-dataflow system** built on top of OpenUSD's
native execution and computation framework (OpenExec). This system allows individual prim attributes within a USD stage
to be driven by external data sources — such as REST APIs that return JSON — via a graph of compute-nodes authored
directly in the USD stage.

The dataflow architecture introduces four custom USD prim types (IsA-schemas) and a C++ runtime that bridges OpenUSD's
pull-based computation model to the push-based update model required by an interactive game engine.

Data moves from a **Datasource prim** through one or more **Compute-Node prims** to reach the attribute of a
**regular scene prim** (e.g. a `Cube`), which in turn updates the corresponding Godot node in the scene tree.

---

## Custom USD Prim Type Overview

The following custom IsA-schemas are compiled into a dedicated OpenUSD plugin library (`libIDTX`) that is linked into
the IDTXFlow GDExtension. They are available in any USD stage loaded by IDTXFlow.

| Custom Prim Type             | Category            | Godot Node                     | Purpose                                                                                              |
|------------------------------|---------------------|--------------------------------|------------------------------------------------------------------------------------------------------|
| `Datasource`                 | Abstract Base       | —                              | Abstract base type for all datasource prims. Declares the `outputs:data` string attribute.           |
| `MockDatasource_RandomFloat` | Datasource (Demo)   | `UsdMockDatasourceFloatNode3D` | **Demo-only.** Simulates a data source generating a random float on a configurable interval as JSON. |
| `Compute_ValueFromJson`      | Compute Node        | —                              | Extracts a typed scalar from a JSON string at a given JSON Pointer path. **Production-ready.**       |
| `Compute_ColorFromFloat`     | Compute Node (Demo) | —                              | **Demo-only.** Maps a float onto a `color3f` via a configurable boundary/color lookup table.         |

---

## Datasource Prims

### Abstract Base: `Datasource`

`Datasource` is a non-instantiatable abstract base prim type that all concrete datasource implementations inherit from.
It defines the single output that the rest of the compute graph depends on:

```usda
class "Datasource" (
    inherits = </Typed>
    doc = "Base class for all data source primitives."
) {
    string outputs:data = "{}"   # JSON-encoded data payload
}
```

Any prim that inherits `Datasource` is expected to keep `outputs:data` up-to-date with a valid JSON string. Downstream
compute-node prims connect their `inputs:jsonData` to this attribute using a USD attribute connection (`inputs:jsonData.connect = <...>`).
Updating the `outputs:data` of the concrete data source prim is not part of the openUSD extension, but is implemented in
the game engine that converts this prim into it's own entity type.

### Demo Datasource: `MockDatasource_RandomFloat`

> __This prim type exists for demonstration purposes only.__ It is not intended for production use.
> Its sole purpose is to show how the dataflow pipeline works end-to-end without requiring an external REST service.

`MockDatasource_RandomFloat` inherits `Datasource` and adds a single configuration attribute:

```usda
class MockDatasource_RandomFloat "MockDatasource_RandomFloat" (
    inherits = </Datasource>
) {
    float interval = 1.0          # update interval in seconds
    string outputs:data = "{}"    # inherited from Datasource
}
```

When the IDTXFlow plugin converts a `MockDatasource_RandomFloat` prim it creates a `UsdMockDatasourceFloatNode3D` Godot node.
This node runs a timer in Godot's `_process` loop and, once the interval elapses, generates a random float in the range `[0, 1]`
and serializes it as:

```json
{ "data": { "value": 0.734 } }
```

and authors this string back into the `outputs:data` attribute of the prim in the live USD stage using the current active
edit target, which is usually the session layer. If `outputs:data` is connected to a downstream compute node, OpenExec detects
the changed input and marks the dependent computation as invalid, causing it to be re-evaluated when computation is triggered
again.

## Compute-Node Prims

### Built-In Compute Node: `Compute_ValueFromJson`

`Compute_ValueFromJson` is the __primary, built-in compute-node type__ to extract values from data sources providing JSON content.
It can be used in any USD stage you author to wire a REST/JSON data source to a scene attribute. It has no dependency
on the mock datasource and works with any prim whose `outputs:data` produces a valid JSON string — including a real HTTP-based
datasource you implement yourself.
The JSON path resolution follows [RFC 6901 JSON Pointer](https://www.rfc-editor.org/rfc/rfc6901) semantics
(e.g. `/data/temperature`, `/sensors/0/value`). Integer tokens in the path are treated as array indices.

```usda
class Compute_ValueFromJson "Compute_ValueFromJson" (
inherits = </Typed>
) {
    string inputs:jsonData = "{}"          # connects to a Datasource outputs:data
    string jsonPath                        # RFC 6901 JSON Pointer, e.g. "/data/value"
    token  jsonValueType                   # "float" | "double" | "string" | "none"

    double outputs:jsonValue:double = 0.0  # populated when jsonValueType = "double"
    float  outputs:jsonValue:float  = 0.0  # populated when jsonValueType = "float"
    string outputs:jsonValue:string = ""   # populated when jsonValueType = "string"
}
```
You can use `Compute_ValueFromJson` in any USD stage you author, as long as it is loaded by IDTXFlow. Connect its `inputs:jsonData`
to any prim that outputs a JSON string (not necessarily a `Datasource` subtype), set `inputs:jsonPath` to the RFC 6901
pointer into that JSON, and choose the output type. Then connect a downstream prim attribute to one of the output attributes:

```usda
def Compute_ValueFromJson "GetTemperature" {
    string inputs:jsonData = "{}"
    string inputs:jsonData.connect = </World/MyRestSource.outputs:data>
    string inputs:jsonPath = "/sensors/ambient/temperature"
    token  inputs:jsonValueType = "double"
    double outputs:jsonValue:double = 0.0
}

def Sphere "TemperatureIndicator" {
    double radius = 1.0 # default value applied to the sphere during convertion
    double radius.connect = </World/GetTemperature.outputs:jsonValue:double>
}
```

### Built-In Compute Node: `Compute_ScaleDouble`

`Compute_ScaleDouble` is the __built-in compute-node type__ that demonstrates a simple computation that could be chained
between the value extraction from a data source and the receiving prim attribute.

```usda
class Compute_ScaleDouble "Compute_ScaleDouble" (
    inherits = </Typed>
    customData = {
        string userDocBrief = "This Schema (PrimType) registers a computation that will multiply the input value with the scalarFactor"
    }
)
{
    double scalarFactor = 1.0 (
        doc = """The scalar factor that shall be multiplied to the input value"""
    )
    double inputs:value = 0.0 (
        doc = """The input value to this computation. Could be connected to another compute node"""
    )
    double outputs:result (
        doc = """The result of the computation"""
    )
}
```
You can use `Compute_ScaleDouble` in any USD stage you author, as long as it is loaded by IDTXFlow. You may connect the `inputs:value`
to the *ValueFromJson* node while the `outputs:result` can feed into any prim attribute of type double.

```usda
def Compute_ValueFromJson "GetTemperature" {
    string inputs:jsonData = "{}"
    string inputs:jsonData.connect = </World/MyRestSource.outputs:data>
    string jsonPath = "/sensors/ambient/temperature"
    token  jsonValueType = "double"
    double outputs:jsonValue:double = 0.0
}

def Compute_ScaleDouble "ScaleTemperature" {
    double scalarFactor = 0.5
    double inputs:value.connect = </World/GetTemperature.outputs:jsonValue:double>
}

def Sphere "TemperatureIndicator" {
    double radius = 1.0 # default value applied to the sphere during convertion
    double radius.connect = </World/ScaleTemperature.outputs:result>
}
```
## The ExecBridge and ExecBridgeManager

### Motivation: Pull vs. Push

OpenExec uses a __pull model__: a consumer requests a computed value, and the framework re-executes only those parts of
the computation graph that have been invalidated since the last request. Game Engines like Godot, on the other hand, need
a __push model__: when a data value changes the corresponding scene node must receive the new value and update its visual
state immediately.

The `ExecBridge` and `ExecBridgeManager` classes bridge this gap.

### `ExecBridge` — Per-Stage Computation Bridge
Each loaded USD stage gets exactly one ExecBridge instance. The bridge owns a `pxr::ExecUsdSystem` for that stage and
maintains two parallel arrays:

- __`exec_value_keys_`__ — a list of `pxr::ExecUsdValueKey` objects, each identifying one (source-prim, computation-name)
pair to be evaluated.
- __`value_key_metas_`__ — a parallel array of ValueKeyMetadata structures that record, for each value key, the originating
attribute path, the last computed value (used for change detection), and the prim path of the handler that should receive
updates.

It also maintains a map of registered result handlers, which are used to dispatch updated compute results to, keyed by prim path:

**result_handlers_: SdfPath → vector<IExecBridgeHandler*>**

The core functionality of the `ExecBridge` is to run the `ComputeAndDispatch()` method. This pulls the actual compute results
from the compute node graph and dispatches them to the registered nodes that has been converted from the respective usd prims.

When nothing in the compute graph has changed (no new data from the datasource), the call to ComputeAndDispatch() completes
instantly with no Godot-side updates. OpenExec itself also maintains an internal result cache; when inputs have not changed
it returns cached outputs without re-executing the computation callbacks. So caching operates at two levels: OpenExec's
own internal cache avoids re-running computation callbacks when the input graph has not changed, and the ExecBridge's
*lastComputedValue* cache avoids dispatching to Godot when the computed output value has not changed.

### `ExecBridgeManager` — Singleton Scheduler

`ExecBridgeManager` is a process-wide singleton (`ExecBridgeManager::Instance()`) that:

1. __Manages one `ExecBridge` per stage__ via `GetExecBridgeForStage(stage)`, creating it on first access.
2. __Maintains an active-bridges list__ — only bridges that have been explicitly activated are included in the periodic computation loop.
3. __Runs a dedicated background worker thread__ that calls `ComputeAndDispatch()` on every active bridge approximately every __100 milliseconds__.

The thread is required to be started by some initialization code within the Game Engine. The *IDTXFlow* plugin does this
at `initialize_idtxflow_module()` and stops the thread at `uninitialize_idtxflow_module()`.

## Prim Conversion and Handler Registration

The connection between USD prim conversion and the ExecBridge machinery is established inside `ConvertPrimPostProcessDefault()`
in `shared/include/idtxflow/converter/StageConverter.h`. This method is called for __every__ successfully converted prim,
immediately after the engine-specific `ConvertPrimPostProcess()` step.

If the converted entity type for the Prim implements the `IExecBridgeHandler` it will be added to the result handler list
of the `ExecBridge` instance for its stage. All computed attribute values for this prim will be delivered via the `OnComputeComplete`
method that is invoked from the `ExecBridge::ComputeAndDispatch()` method in one batch. The implementation of `OnComputeComplete`
is game engine specific and needs to provide the actual handling or mapping of the passed attributes and values - provided in the
`ExecComputeResult` structure - into the properties of the respective node/entity it is implemented for.

## Implement a new Compute Node

This section walks through every step required to implement a custom computation node. The `Compute_ScaleDouble`
node is used as a concrete reference throughout. That node multiplies a fixed value to a `double` input value, and it
supports receiving the double input value either from a directly authored prim attribute **or** from a connected upstream prim.

A compute node for the IDTX system consists of three files:

| File                               | Role                                                                                                                 |
|------------------------------------|----------------------------------------------------------------------------------------------------------------------|
| `schema.usda` entry                | Declares the USD prim type with its input and output attributes.                                                     |
| `computation_<name>.cpp`           | Implements the computation logic using `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA`.                                      |
| `plugInfo.json` (`"Exec"` section) | Tells OpenExec which schemas have registered computations so the library is loaded when those prims are encountered. |

### How the Pieces Fit Together

```
usd/source/schema.usda
  class Compute_ScaleDouble (inherits = </Typed>)
    double scalarFactor = 1.0
    double inputs:value = 0
    double outputs:result
        |
        | usdGenSchema generates C++ schema class + tokens
        v
  IDTXCompute_ScaleDouble (C++ class, UsdTyped subclass)
  IDTXTokens->inputsValue, IDTXTokens->outputsResult, ...

usd/generated/plugInfo.json
  "Exec": { "Schemas": { "IDTXCompute_ScaleDouble": {} } }
        |
        | OpenExec discovers this schema has a computation
        v
  EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(IDTXCompute_ScaleDouble) { ... }
        |
        v
  Downstream prim attribute connection resolved:
    def Sphere "Indicator" {
        double radius.connect = </World/MyScalingNode.outputs:result>
    }
        |
        v
  ExecBridge::ComputeAndDispatch()
    -> IExecBridgeHandler::OnComputeComplete(results)
        |
        v
  Game-engine node updates its visual state
```

---

### Step 1 — Add the Schema to `schema.usda`

Every compute node starts as a USD IsA-schema declared in `usd/source/schema.usda`. Define the new prim type, declare
all input attributes with the `inputs:` namespace prefix and all output attributes with the `outputs:` prefix.

```usda
class Compute_ScaleDouble "Compute_ScaleDouble" (
    inherits = </Typed>
    customData = {
        string userDocBrief = "This Schema (PrimType) registers a computation that will multiply the input value with the scalarFactor"
    }
)
{
    double scalarFactor = 1.0 (
        doc = """The scalar factor that shall be multiplied to the input value"""
    )
    double inputs:value = 0.0 (
        doc = """The input value to this computation. Could be connected to another compute node"""
    )
    double outputs:result (
        doc = """The result of the computation"""
    )
}
```

After editing `schema.usda`, run `scons` (that runs `usdGenSchema` as on of it's steps) to regenerate the C++ schema class (`compute_ScaleDouble.h/.cpp`)
and the token constants (`tokens.h/.cpp`). The generator creates one `TfToken` constant in `IDTXTokensType` for
every attribute name in the schema (e.g. `IDTXTokens->inputsValue`, `IDTXTokens->outputsResult`, `IDTXTokens->scalarFactor`).
Always use these generated tokens — never raw string literals — in the computation implementation.

---

### Step 2 — Register the Schema in `plugInfo.json`

OpenExec discovers which schemas have registered computations through the `"Exec"` section of `plugInfo.json`. Add
your new schema C++ class name to the `"Schemas"` map:

```json
{
    "Plugins": [{
        "Info": {
            "Exec": {
                "Schemas": {
                    "IDTXCompute_ScaleDouble": {},
                  ... 
                }
            },
            "Types": { ... }
        }
    }]
}
```

No additional type entries are needed for the computation itself. The `"Schemas"` map is purely a discovery hint
that causes OpenExec to look for the `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA` block when a prim of this type is
encountered in a stage.

---

### Step 3 — Implement the Computation with `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA`

Create a new `.cpp` file (e.g. `computation_scaledouble.cpp`) that includes `registerSchema.h`, `vdf/context.h`,
and the generated `tokens.h`. The entire computation is registered with the block macro `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(SchemaClass)`.

Inside the block, `self` refers to a schema builder object that exposes two registration methods:

- **`self.PrimComputation(outputToken)`** — registers the core computation. This is where the business logic lives.
  It declares which inputs it needs and provides a callback that is invoked by OpenExec whenever the output needs
  to be recomputed.
- **`self.AttributeComputation(outputToken, bridgeToken)`** — registers a thin forwarding computation that makes
  the prim's computed output reachable as a USD attribute connection target. Without this, downstream prims cannot
  connect to the output via `outputs:result.connect = <...>`.

#### The Computation Implementation

```cpp
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
                .Connections<float>(_IDTXTokens->resolvedValue)
                .InputName(_IDTXTokens->connectedInputsValue),
            // Also request the raw authored attribute value as a fallback.
            AttributeValue<float>(IDTXTokens->inputsValue)
            // The scalarFactor input
            AttributeValue<double>(IDTXTokens->scalarFactor).Required()
        );
        
    // Prim-To-Attribute Glue
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
```

##### The Prim-To-Attribute glue

The `AttributeComputation` makes the prim's computed output reachable as a USD attribute connection target. Without
it a downstream prim cannot author `double radius.connect = </Scene/MyNode.outputs:result>` and
have OpenExec follow that connection.

The callback simply forwards the prim computation result to the attribute output. The `.Inputs()` declaration with
`Prim().Computation<T>(outputToken)` is what wires the two computations together — it tells OpenExec that this
attribute computation depends on the named prim computation above. This glue code should be present for all custom
computations that shall be used in a compute node chain.

```cpp
    self.AttributeComputation(IDTXTokens->outputsResult, _IDTXTokens->resolvedValue)
        .Callback<T>(+[](const VdfContext& ctx)
        {
            // Forward the prim computation result to the attribute output
            // so that downstream USD attribute connections can read it.
            ctx.SetOutput<T>(
                ctx.GetInputValue<T>(IDTXTokens->outputsResult)
            );
        })
        .Inputs(
            // Declare a dependency on the PrimComputation registered above.
            Prim().Computation<T>(IDTXTokens->outputsResult)
        );
```

##### Why Two Computations?

OpenExec distinguishes between *prim computations* (owned by the prim that authors the value) and *attribute
computations* (owned by the attribute on a consumer prim that receives a connection). When a consumer prim
authors `myAttr.connect = </Scene/MyNode.outputs:result>`, OpenExec needs an attribute computation registered
under the name `outputs:result` on `MyNode`'s schema. The `AttributeComputation` declaration fulfils this
requirement. The two computations share results through the private `resolvedValue` bridge token, which acts
as the named link between the prim computation's output and the attribute computation's input.

#### Keeping Both the Authored Value and the Connected Value

The pattern used in `.Inputs()` is the key to supporting both cases simultaneously:

```cpp
.Inputs(
    Attribute(IDTXTokens->inputsValue)          // attribute with connection support
        .Connections<T>(resolvedValue)          // if connected: evaluate source & deliver here
        .InputName(connectedInputsValue),       // alias for GetInputValuePtr inside callback
    AttributeValue<T>(IDTXTokens->inputsValue)  // also: raw authored value
)
```

Inside the callback you then use:

```cpp
// Check for a live connection first:
if (const double* ptr = ctx.GetInputValuePtr<double>(_IDTXTokens->connectedInputsValue))
    value = *ptr;             // connection present and resolved
else
    value = ctx.GetInputValue<double>(IDTXTokens->inputsValue);  // authored fallback
```

`GetInputValuePtr` returns `nullptr` when no connection was resolved for the given input name; `GetInputValue`
always returns a value (the authored default if nothing else). This pattern should be followed for every input
that supports optional upstream connections.

---

### Step 4 — Node Registration Summary

The following checklist covers every artefact that must be in place for a new compute node to work end-to-end:

| # | Artefact                               | What to do                                                                                         |
|---|----------------------------------------|----------------------------------------------------------------------------------------------------|
| 1 | `usd/source/schema.usda`               | Add a new `class` block with `inputs:*` and `outputs:*` attributes.                                |
| 2 | Run `usdGenSchema`                     | Regenerates `compute_<Name>.h/.cpp` and `tokens.h/.cpp` with the new `TfToken` constants.          |
| 3 | `usd/generated/plugInfo.json`          | Add the schema C++ class name to `"Exec" > "Schemas"`.                                             |
| 4 | `usd/generated/computation_<name>.cpp` | Implement `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA` with `PrimComputation` + `AttributeComputation`. |
| 5 | Build `libIDTX`                        | Compile the new `.cpp` into the shared library. Done by `scons` automatically in this repo.        |

Once the library is rebuilt and the `plugInfo.json` is deployed alongside it, any USD stage that contains a prim of
the new schema type will have its computation automatically discovered and wired into the OpenExec graph by the
IDTXFlow runtime.
