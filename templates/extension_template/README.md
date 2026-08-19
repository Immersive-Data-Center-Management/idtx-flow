# IDTXFlow Extension Template

This is a starter template for building a GDExtension plugin that extends **IDTXFlow** — the OpenUSD integration for Godot Engine.

## What You Can Extend

| Extension Type | How |
|---|---|
| **Custom USD prim converters** | Implement `IPrimConverter<TargetEngineGodot>` and register it in the `PrimConverterRegistry` |
| **Custom Godot nodes** | Inherit from Godot classes + `IUsdNode3D`, register with `GDREGISTER_CLASS` |
| **Editor UI / Inspector plugins** | Add GDScript `EditorPlugin` in your addon directory |

## Directory Structure

```
my-extension/
├── SConstruct                                  # Build script
├── README.md
├── scons/
│   ├── godotcpp.py                             # scons tool to download and compile C++ bindings for the extension
│   └── gdextension.py                          # scons tool to actually build the GDExtension
├── addons/
│   └── MyIdtxflowExtension/
│       ├── my_idtxflow_extension.gdextension   # Godot extension descriptor
│       └── bin/                                # Built binaries (per platform)
├── source/
│    ├── register_types.h                        # GDExtension entry point
│    ├── register_types.cpp
│    ├── converters/
│    │   └── ExamplePrimConverter.h              # Example IPrimConverter implementation
│    └── nodes/                                  # Custom Godot node classes (optional)   
└── thirdparty/
    └── idtxflow-sdk/                           # the IDTXFlow sdk files and folders
```

## Prerequisites

1. The SDK of **IDTXFlowGodot** must be copied to the `thirdparty` folder.

2. **godot-cpp** must be built (this is what the `godotcpp` scons tool does).

3. Use the same immutable godot-cpp release as IDTXFlow. The template pins
   godot-4.5-stable; do not replace it with the moving 4.5 branch.

## Building

```bash
scons target=template_debug
```

The built shared library will be placed in `addons/MyIdtxflowExtension/bin/<platform>/`.

## Installation in a Godot Project

1. Copy `addons/MyIdtxflowExtension/` into your Godot project's `addons/` directory.
2. Ensure `addons/IDTXFlow/` is already installed.
3. Godot will automatically load both extensions, respecting the dependency order declared in the `.gdextension` file.

## Creating a Custom Converter — Checklist

1. Create a header in `source/converters/MyConverter.h`
2. Inherit from `IPrimConverter<idtxflow::types::TargetEngineGodot>`
3. Implement `GetSupportedPrimTypes()`, `GetPriority()`, `GetConverterName()`, `Convert()`, `PostProcess()`
4. In `register_types.cpp`, call `Registry::Instance().Register(std::make_shared<MyConverter>())`
5. In `register_types.cpp` terminator, call `Registry::Instance().Unregister("MyConverter")`

### Cross-GDExtension object rule

IPrimConverter transports ConvertedEntityHandle values, not raw godot-cpp
wrapper pointers. In Convert(), return
Types::GetConvertedEntityHandle(node). In PostProcess(), resolve the received
handles with Types::ResolveConvertedEntity() before using the nodes.

Each GDExtension owns a distinct godot-cpp instance-binding token. Passing a
godot::Node3D pointer to another DLL lets that DLL dereference a foreign C++
wrapper and can crash even though the underlying Godot object is valid.

## Creating a Custom Node

If your converted prim needs a custom Godot node class:

1. Create a class inheriting from a Godot node type (e.g., `Node3D`, `Camera3D`)
2. Also inherit from `IUsdNode3D` for USD metadata integration
3. Use `IUSDNODE(MyNode, false)` for a regular USD node, or pass `true` when the node also implements `IExecBridgeHandler`
4. Use the `IUSDNODE_IMPLEMENT_BINDINGS` macro in `_bind_methods()`
5. Register with `GDREGISTER_CLASS()` in your `initialize_*_module()` function
