[![REUSE status](https://api.reuse.software/badge/github.com/Immersive-Data-Center-Management/idtx-flow)](https://api.reuse.software/info/github.com/Immersive-Data-Center-Management/idtx-flow)

# IDTX Flow

## About this Project

Immersive Digital Twin Experience Flow plugin for Godot. The plugin enables the import of Universal Scene Description (USD) files into Godot.

## Using the Plugin

To use the plugin within a Godot 4.5+ project, the required binaries can be downloaded from the release page of this repository. The extracted `IDTXFlow` folder
from the package shall be copied to the *addons* folder of the Godot project.

## Requirements and Setup to Build from Source

The IDTX Flow plugin for Godot uses `scons` (a python build tool) to download and install all required dependencies to run the build that creates the binaries.
However, some tools and software are still required to be installed upfront.

The following software and components are required to be installed first. Usually prior or directly after cloning the repository.

### All Operating Systems

- **Python3**, once installed use `pip` to install `scons`, `jinja2` and `pyside6`
- **Godot4.5**, to be able to test the plugin within a Godot project. The Godot version need to match the version of the [C++ bindings](https://github.com/godotengine/godot-cpp) used as a dependency.

### Windows

- **C++ Buildtools** - MS Visual Studio Build tools
- **CMake** - usually part of the MS Visual Studio Build tools

### MacOS

 - **C++ Buildtools** - The full Xcode IDE is required because OpenUSD's build script uses xcodebuild for codesigning,
 - **CMake** - use `brew install cmake` for example.

Once all required software and tools are installed and configured the plugin can be build with the following command, executed at the root folder of this repository.

```bash
checked_out_repo_dir $>scons
```

> Please be patient, as the first initial build will download and compile openUSD from source. Depending on the used hardware this may take up to 40 minutes or more.

The binary artifacts and all additional required files for the plugin can be found in the folder `addon/IDTXFlow`. Just copy this folder into the `addons` folder of the Godot project. To verify and enable the plugin, open the project in Godot, go to **Project → Project Settings → Plugins**, find the plugin and click the respective checkbox.

## Features

### Godot Nodes

The plugin provides several custom 3d nodes that are used within the scene tree to represent the converted contents of a USD stage.

|  Custom Node Type | Inherits from | Purpose / Description |
|----|---|----|
| UsdStageNode3D | Node3D | This is the node that should be added manually to the scene tree, if the contents of an USD stage shall be converted into it. Once, the URI to an USD file (*.usd, *.usda, *.usdc or *.usdz) is provided, the conversion happens. The conversion is executed within the editor as well as during runtime. The URI can locate files at `res://`, `user://`, `http://` or `https://` locations. However, there is no athentication supported for remote locations. Remote USD files will be downloaded into a subfolder of `user://usd_cache/` and opened from this location. Opening a USD file will run full composition. However, payload rferences will not be immediately loaded. If a composed stage contains a payload, a UsdStageNode3D will be created as child to this one with the payload URI configured to initiate the conversion of the referenced USD file. if the "outer" stage has been loaded from a remote location, this remote location would be the resource anchor of the payload, thus relative paths will lead to a remote location as well. |
| UsdXFormNode3D | Node3D | This corresponds to the `Xform` prim type of the USD stage. It only provides a transform without any visualization. The node may contain animation data that drives the transform over time. |
| UsdMeshInstanceNode3D | MeshInstanceNode3D | This corresponds to all `Geom` prim types that contain visual geomitry including the primitives like Cube, Sphere, Cone, Cylinder. |
| UsdMultiMeshInstanceNode3D | MultiMeshInstanceNode3D | This corresponds to instanced `Geom` prim types. However, the conversion does not support real instancing of prims within an USD stage, yet. This node is used, when *pseudo-instancing* appears within the USD stage as an optimization step modeling tools use during export into the [openUSD](https://openusd.org/release/index.html) format. This means, that the stage contains mesh definitions as `over` (without another spec and thus being invisible), that are referenced by *"Instances"* within the same stage. This pattern is used, when the complexity of full istancing should be omitted, but copying the same prim (mesh) mutliple times within the same stage should be avoided to reduce file size and memory footprint, when loading a stage.  |
| UsdSkeletonNode3D | Skeleton3D | This corresponds to a `Skeleton` prim, defining the bones and other attributes. Usually, this node will contain the meshes used for skinning this skeleton as UsdMeshInstanceNode3D children. Animation data, if any is authored for the bones, is stored within this node. |

### Converted openUSD Prim Types

The following list provides a complete overview of the actual prim types that will be converted into Godot entities. It may repeat some of the nodes mentioned above.

| USD Prim Type | Godot Entity | Remarks |
|---|---|---|
| Xform | UsdXFormNode3D |  |
| Cube | UsdMeshInstanceNode3D | Uses a `BoxMesh` |
| Cylinder | UsdMeshInstanceNode3D | Uses a `CylinderMesh`. The Prim's Axis attribute is used to rotate the cylinder in Godot to match the expected orientation defined by the axis attribute value. |
| Cone | UsdMeshInstanceNode3D | Uses a `CylinderMesh` with top radius set to 0.0. The Prim's Axis attribute is used to rotate the cylinder in Godot to match the expected orientation defined by the axis attribute value. |
| Sphere | UsdMeshInstanceNode3D | Uses a `SphereMesh`. |
| Mesh | UsdMeshInstanceNode3D | Uses an `ArrayMesh`. If the prim contains `GeomSubset`'s they will be converted into surfaces added to the ArrayMesh. |
| SkelRoot | - | This is the openUSD anchor Prim for any skeleton contained within. It would be an error if a Skeleton Prim exists without this one as parent. However, it will not convert into a Godot entity as such. |
| Skeleton | UsdSkeletonNode3D | The Godot skeleton will maintain the same bone hierarchy as provided by openUSD. However, there might be no limitations on how many skinning weights can be assigned to a single bone in usd. As Godot only supports up to 4 bone weights, only the first 4 values will be taken from usd. This might lead to artifacts in bone skinning as the sum of all weights will not add up to 1.0. |
| Material | StandardMaterial3D | Creating a Godot StandardMaterial3D from the USD Prim type Material might require to follow references to Shader Prim types in the USD files as those shader might define varying values for specific material values like albedo color, normals, roughness or the like. Those sources can be references to texture images, that will create `Image` and `Texture2D` entities. |
| Shader | - | The shader nodes will be used to extract the required information to create a `StandardMaterial3D`, but will convert into different Godot entity types, based on their usage. |

### Pseudo Instancing Example

The following example demostrates the pseudo-instancing.

```usda
#usda 1.0
(
    defaultPrim = "Bolts"
)

#########################################
# 1. Prototype definition (over)
#########################################

over "BoltPrototype"
{
    def Mesh "Body"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0,0,0), (1,0,0), (0,1,0)]
    }
}

#########################################
# 2. “Instances” created via references
#########################################

def Xform "Bolts"
{
    def Xform "Bolt1" (
        prepend references = </BoltPrototype>
    )
    {
        
        double3 xformOp:translate = (0, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]

        # optional: override the prototype's child prim
        over "Body" { }
    }

    def Xform "Bolt2" (
        prepend references = </BoltPrototype>
    )
    {
        double3 xformOp:translate = (2, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Xform "Bolt3" (
        prepend references = </BoltPrototype>
    )
    {        
        double3 xformOp:translate = (4, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
```

### Handling of Payloads

If a prim in a composed stage contains an authored payload like the following, this prim is not immediately loaded during stage composition. Instead the conversation logic will create a UsdStageNode3D entity passing the URI of the payload to trigger loading and converting the referenced stage.

```usda
def "PayloadPrim" (
  prepend payload = @./external.usd@
)
{
}
```

The challenge in this setup is, that the actual stage, that authored the payload, may also author its own opinion on properties or contained prims of the referenced layer.

```usda
def "PayloadPrim" (
  prepend payload = @./external.usd@
)
{
  double3 xformOp:translate = (100.0, 0.0, 0.0)
  uniform token[] xformOpOrder = ["xformOp:translate"]

  over "ChildPrim" {
    color3f[] primvars:displayColor = [(1, 0, 0)]
  }
}
```

To ensure, that, those opinions will not get lost, they will be transferred into a `SessionLayer` that is used when composing the stage of the referenced USD file. This session layer is anchored at the same "location" as the stage the reference was authored in. This ensures, that relative paths can be successfully resolved as expected. This means, the UsdStageNode3D will be created as child node containing the URI to the USD file and the session layer contents.

### Material Conversion

The initial version will create `StandardMaterial3D` instances based on the material data authored within the USD layer. One of the challenges by doing so, is that the shader nodes authored within the USD layer might not be able to map to the Godot material 1:1. Further more
the `StandardMaterial3D` supports one UV set accross the different texture channels for the material, while the shader nodes of openUSD might author multiple and different ones. Thus, the conversion logic uses the UV sets named `st` or `st0`, only. If a shader input links to a shader node that references a texture file, this file will be loaded as `Image` and used as `Texture2D` for the respective material channel. The plugin maintains an internal cache of those files to allow re-use of the same images and textures accross different materials.

The following table shows how the USD shader inputs map the the corresponding material channels/properties of the `StandardMaterial3D`.

| USD Shader Input | Godot Material Channel | Remarks |
|---|---|---|
| diffuseColor | Albedo | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| metallic | Metallic | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| roughness | Roughness | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| specular | Specular | When linked to a shader that provides a texture, this can't be reflected in Godot. |
| ior | Specular | The Specular value will be calculated as ((ior - 1)/(ior + 1)²) / 0.08, if the IOR value is > 5.0 the Metallic channel of the material will be set to 1.0 |
| emissiveColor | Emission | This will activate the material feature *EMISSION*. When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| normal | Normal | When linked to a shader that provides a texture, it will bind this texture to UV1 coordinates of the mesh. |
| occlusion | AmbientOcclusion | This will activate the material feature *AMBIENT_OCCLUSION*. When linked to a shader that provides a texture, it will bind this texture to UV1 coordinated of the mesh. |
| opacityThreshold | AlphaScissor | This will set the material transparency mode to *ALPHA_SCISSOR*. The threshold will be checked against the albedo alpha channel. If albedo is retrieved from a texture with alpha channel, then the regions with alpha > threshold appear as cut-outs. |
| opacity | Transparency | The opacity value will be combined with the albedo alpha channel. When linked to a shader that provides a texture, this can't be reflected in Godot. |

## Support, Feedback, Contributing

This project is open to feature requests/suggestions, bug reports etc. via [GitHub issues](https://github.com/Immersive-Data-Center-Management/idtx-flow/issues). Contribution and feedback are encouraged and always welcome. For more information about how to contribute, the project structure, as well as additional contribution information, see our [Contribution Guidelines](CONTRIBUTING.md).

## Security / Disclosure

If you find any bug that may be a security problem, please follow our instructions at [in our security policy](https://github.com/Immersive-Data-Center-Management/idtx-flow/security/policy) on how to report it. Please do not create GitHub issues for security-related doubts or problems.

## Code of Conduct

We as members, contributors, and leaders pledge to make participation in our community a harassment-free experience for everyone. By participating in this project, you agree to abide by its [Code of Conduct](https://github.com/Immersive-Data-Center-Management/.github/blob/main/CODE_OF_CONDUCT.md) at all times.

## Licensing

Copyright 2026 SAP SE or an SAP affiliate company and idtx-flow contributors. Please see our [LICENSE](LICENSE) for copyright and license information. Detailed information including third-party components and their licensing/copyright information is available [via the REUSE tool](https://api.reuse.software/info/github.com/Immersive-Data-Center-Management/idtx-flow).
