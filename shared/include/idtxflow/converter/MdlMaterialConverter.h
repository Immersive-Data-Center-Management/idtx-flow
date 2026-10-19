#pragma once
#ifdef IDTXFLOW_MDL_ENABLED
/**
 * @file MdlMaterialConverter.h
 * @brief The MdlMaterialConverter class allows to extract properties from a material within the usd layer, that refers
 * a MDL shader file.
 *
 * These properties can be used to create a base PBR based material that does not use shader nodes in the
 * target engine. Creating the actual engine specific material, based on those properties will be implemented in the
 * TypeConverter specialization of the toMaterial method.
 *
 * To actually enable the access to the mdl SDK to read the contents of the mdl files there is some preparation work
 * required, which includes dynamic loading of the respective library and accessing the exposed API entrypoint. Thus
 * without calling the StartupMdlMaterialConverter() function this class will not be able to provide any conversion.
 *
 **/

#include <filesystem>
#include <regex>
#include <optional>

#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usdShade/material.h>

#include <mi/base/config.h>
#include <mi/base/handle.h>
#include <mi/neuraylib/factory.h>
#include <mi/neuraylib/idatabase.h>
#include <mi/neuraylib/version.h>
#include <mi/neuraylib/iscope.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/imdl_configuration.h>
#include <mi/neuraylib/imdl_impexp_api.h>
#include <mi/neuraylib/imdl_factory.h>
#include <mi/neuraylib/imdl_execution_context.h>
#include <mi/neuraylib/ineuray.h>
#include <mi/neuraylib/iplugin_configuration.h>
#include <mi/neuraylib/itransaction.h>
#include <mi/neuraylib/iversion.h>
#include <mi/neuraylib/istring.h>
#include <mi/neuraylib/imodule.h>
#include <mi/neuraylib/itexture.h>
#include <mi/neuraylib/iimage.h>

#include "../types/MaterialTypes.h"
#include "../types/TargetTypes.h"
#include "../converter/AssetConverter.h"

#ifndef MI_PLATFORM_WINDOWS
#include <dlfcn.h>
#endif

namespace idtxflow
{
namespace converter
{
// this flag indicates if the required preparation to work with mdl files and the mdl SDK has been executed
inline constinit bool mdlConversionInitialized = false;
// this handle is set, once the mdl SDK library is loaded
#ifdef MI_PLATFORM_WINDOWS
inline constinit HMODULE mdlLibraryHandle = nullptr;
#else
inline constinit void* mdlLibraryHandle = nullptr;
#endif
// this is the pointer to the mdlSDK entry point of the dynamically loaded library
inline constinit mi::neuraylib::INeuray* neurayEntry = nullptr;

inline mi::base::Handle<mi::neuraylib::IMdl_factory>& mdl_factory()
{
    static mi::base::Handle<mi::neuraylib::IMdl_factory> mdlFactory;
    return mdlFactory;
};

inline mi::base::Handle<mi::neuraylib::ITransaction>& mdl_transaction()
{
    static mi::base::Handle<mi::neuraylib::ITransaction> mdlTransaction;
    return mdlTransaction;
}

/**
 * Perform required setup to enable the use of the mdl sdk and thus enable the conversion of usd material shader
 * referencing mdl shader files.
 * @param mdlBaseDir The directory the mdl library and mdl extension libraries will be loaded from
 * @param mdlModuleDir The directory where core/base mdl shader modules could be found
 * @param additionalMdlModuleDirs A list of directories that will be added to the search path for mdl modules
 * @return
 */
inline bool StartupMdlMaterialConverter(const std::string& mdlBaseDir, const std::vector<std::string>& mdlModuleDirs)
{

    // to enable the mdl conversion we need to dynamically load the mdl library and make it's symbols available
    // during runtime of the current executable. The library is required to be located at mdlBaseDir!
    std::string mdlLibraryFile = mdlBaseDir + "/libmdl_sdk" MI_BASE_DLL_FILE_EXT;
#ifdef MI_PLATFORM_WINDOWS
    HMODULE libraryHandle = LoadLibraryA(mdlLibraryFile.c_str());
    if (!libraryHandle) return false;

    void* mdlFactorySymbol = GetProcAddress(libraryHandle, "mi_factory");
    if (!mdlFactorySymbol) return false;
#else
    void* libraryHandle = dlopen(mdlLibraryFile.c_str(), RTLD_LAZY);
    if (!libraryHandle) return false;
    void* mdlFactorySymbol = dlsym(libraryHandle, "mi_factory");
    if (!mdlFactorySymbol) return false;
#endif
    mdlLibraryHandle = libraryHandle;
    // get the global access point into the mdl factory provided by the DLL
    mi::neuraylib::INeuray* neuray = mi::neuraylib::mi_factory<mi::neuraylib::INeuray>(mdlFactorySymbol);
    // if we can't get the entrypoint, check if this is because of a version missmatch
    if (!neuray)
    {
        mi::base::Handle<mi::neuraylib::IVersion> neurayVersion(
            mi::neuraylib::mi_factory<mi::neuraylib::IVersion>(mdlFactorySymbol));
        // without a version info the library file provided is incompatible with the headers used
        if (!neurayVersion) return false;
        assert(std::string(neurayVersion->get_product_version()) == MI_NEURAYLIB_PRODUCT_VERSION_STRING);
        // even with matching versions, the entrypoint could not be found - so we are unable to continue
        // with initialization
        return false;
    }

    neurayEntry = neuray;

    // next we need to register certain plugins to be able to access contents of the mdl files or content those
    // definitions refer to - most crucial: texture (image) data. It is required that the plugins (which are dll's)
    // are located next to the mdl library.
    mi::base::Handle<mi::neuraylib::IPlugin_configuration> neurayPlugins(
        neurayEntry->get_api_component<mi::neuraylib::IPlugin_configuration>());

    // TODO: verify if we really need this extension
    std::string imagePlugin = mdlBaseDir + "/nv_openimageio" MI_BASE_DLL_FILE_EXT;
    neurayPlugins->load_plugin_library(imagePlugin.c_str()); // TODO: how to handle if this is unable to load?

    // TODO: verify if we really need this extension
    std::string ddsPlugin = mdlBaseDir + "/dds" MI_BASE_DLL_FILE_EXT;
    neurayPlugins->load_plugin_library(ddsPlugin.c_str()); // TODO: how to handle if this is unable to load?

    // TODO: verify if we really need this extension
    std::string distillerPlugin = mdlBaseDir + "/mdl_distiller" MI_BASE_DLL_FILE_EXT;
    neurayPlugins->load_plugin_library(distillerPlugin.c_str()); // TODO: how to handle if this is unable to load?

    // as shaders in the mdl format can refer to other shaders (modules) they are extending or referring to,
    // we need to add search paths to the mdl library, as the internal implementation will only look into those
    // folders to find the respective mdl files. It is expected, that the core mdl files are stored in the mdlModuleDir
    // folder
    mi::base::Handle<mi::neuraylib::IMdl_configuration> mdlConfig(
        neurayEntry->get_api_component<mi::neuraylib::IMdl_configuration>());
    for (const auto& modulePath: mdlModuleDirs)
    {
        mdlConfig->add_mdl_path(modulePath.c_str());
    }

    // now we can start-up the library, which might spawn threads and an internal runtime. Only after this is done
    // the library can be properly used to access mdl contents. Don't forget to shut the library down before the
    // using executable stops and unloads from memory. This could be done with: DisableMdlMaterialConversion
    neurayEntry->start();

    mdl_factory() = neurayEntry->get_api_component<mi::neuraylib::IMdl_factory>();

    // with the library startet we can now finalize the initialization and instantiate required objects and
    // start a transaction that tracks all the mdl related resourcess and allows access to them
    mi::base::Handle<mi::neuraylib::IDatabase> mdlDatabase(neurayEntry->get_api_component<mi::neuraylib::IDatabase>());
    mi::base::Handle<mi::neuraylib::IScope> mdlScope(mdlDatabase->get_global_scope());

    mdl_transaction() = mdlScope->create_transaction();

    mdlConversionInitialized = true;

    return mdlConversionInitialized;
}

inline void ShutdownMdlMaterialConverter()
{
    // closing the transaction used for all resource accesses. It has to be ensured, that there are no open handles
    // of mi::base::Handle existing or any raw object pointer in use that did not make use of the mdl internal
    // ref-counting and thus is not properly released.
    if (mdl_transaction())
    {
        mdl_transaction()->abort();
        mdl_transaction()->release();
    }
    if (neurayEntry) neurayEntry->shutdown();
#ifdef MI_PLATFORM_WINDOWS
    FreeLibrary(mdlLibraryHandle);
#else
    dlclose(mdlLibraryHandle);
#endif
}

template <typename TargetEngine>
    requires types::ValidTargetEngine<TargetEngine>
class UsdMdlMaterialConverter
{
  public:
    using TargetTypes = types::TargetEngineTypes<TargetEngine>;
    using TextureType = typename TargetTypes::Texture;

    /**
     * Convert an UsdShadeMaterial prim into game engine specific data structures. It is the responsibility
     * of the calling implementation to prevent repetitive conversion of the same material to save performance
     * and resources during the conversion process.
     * @param material
     */
    std::optional<types::MaterialDescription<TextureType>> Convert(const pxr::UsdShadeMaterial& usdMaterial,
                                                                   const pxr::UsdShadeOutput& shadeOutput)
    {
        // the MDL based materials will reference a mdl shader prim that stores the path to the shader file
        pxr::UsdShadeOutput::SourceInfoVector sourceInfos = shadeOutput.GetConnectedSources();
        if (sourceInfos.empty()) return {}; // no valid connected source -> no shader -> no material

        // we assume there will always only be one source connected to the shaderOutput
        if (!sourceInfos[0].source) return {};

        // from the connected shader we can retreive the path to the MDL asset
        pxr::UsdShadeConnectableAPI& shader = sourceInfos[0].source;
        pxr::SdfAssetPath sourceAsset;
        pxr::UsdAttribute attribute = shader.GetPrim().GetAttribute(pxr::TfToken("info:mdl:sourceAsset"));
        // if the connected shader does not provide the asset path to the mdl shader, there is no material.
        if (!attribute.IsValid() || !attribute.Get(&sourceAsset)) return {};

        types::MaterialDescription<TextureType> material;
        material.id = usdMaterial.GetPath().GetString();

        // with the asset path known we can load the mdl module the asset path is pointing to
        mi::base::Handle<const mi::neuraylib::IFunction_definition> materialDefinition =
            getMaterialDefinitionFromMdlModule(sourceAsset, usdMaterial.GetPrim().GetStage());
        if (!materialDefinition) return {};

        // To create the MaterialDescription based on the MDL material we will combine the default shader attribute
        // values extracted from the mdl material with the values provided by the shader prim. This gives us all
        // values we require to setup the MaterialDescription
        // first retrieve the default values of the material from the definition
        mi::base::Handle<const mi::neuraylib::IExpression_list> mdlDefaults(materialDefinition->get_defaults());
        // now run through the default values and check if the usd shader prim has provided a value.
        for (mi::Size p = 0; p < materialDefinition->get_parameter_count(); ++p)
        {
            std::string parameterName = std::string(materialDefinition->get_parameter_name(p));
            if (parameterName == "diffuse_tint")
            {
                class pxr::GfVec3f color = GetColor(parameterName, shader, mdlDefaults);
                material.channels.emplace(types::MaterialChannel::Diffuse,
                                          pxr::GfVec4f(color[0], color[1], color[2], 1.0f));
                continue;
            }

            if (parameterName == "diffuse_texture")
            {
                std::string textureFilePath = GetTextureFilePath(parameterName, shader, mdlDefaults);
                SetMaterialChannelTexture(material, types::MaterialChannel::Diffuse, textureFilePath, usdMaterial);
                continue;
            }

            if (parameterName == "normal_texture" || parameterName == "normalmap_texture")
            {
                std::string textureFilePath = GetTextureFilePath(parameterName, shader, mdlDefaults);
                SetMaterialChannelTexture(material, types::MaterialChannel::Normal, textureFilePath, usdMaterial);
                continue;
            }

            if (parameterName == "ORM_texture")
            {
                std::string textureFilePath = GetTextureFilePath(parameterName, shader, mdlDefaults);
                SetMaterialChannelTexture(material, types::MaterialChannel::ORM, textureFilePath, usdMaterial);
                continue;
            }
        }

        return material;
    }

  protected:
    mi::base::Handle<const mi::neuraylib::IFunction_definition> getMaterialDefinitionFromMdlModule(
        const pxr::SdfAssetPath& mdlPath, const pxr::UsdStageWeakPtr& stage)
    {
        std::string mdlPackageName;

        // loading the mdl module to access the contained material definition from the provided asset path can be
        // a bit "tricky". The reason: If the path is authored as a relative path it seem to be common, that the
        // first "parent path" indicator "../" refers to "leave the current package" instead of actualy referring
        // to the parent path. Thus the relative path (relative to the usd stage the asset is referenced from)
        // authored as "../material.mdl" might actually point to the file stored as sibbling to the usd stage file
        // and not its parent folder. While the authored reletaive path "../../material.mdl" would refer to a file
        // stored in the parent folder of the usd stage in this scenario.
        // As there does not seem to be any reliable way to know whether "../" refers to package escape or
        // the actual parent folder, we can only check of the referenced asset exists at one of them.
        // A relative path authored as "./" can be treated as the usual meaning "same path".
        if (mdlPath.GetAuthoredPath().starts_with("../") || mdlPath.GetAuthoredPath().starts_with("./"))
        {
            std::string checkedRelativePath = mdlPath.GetAuthoredPath();
            // resolving the asset path with the current stage as "ancor" will do an existence check for the file
            // and does not return a valid resolved path if the file does not exist
            pxr::ArResolvedPath resolvedMdlPath = UsdAssetConverter<TargetEngine>::ResolvePath(mdlPath, stage);
            if (resolvedMdlPath.IsEmpty() && !mdlPath.GetAuthoredPath().starts_with("./"))
            {
                // resolving the relative path as is (assuming "../" means parent folder) was not successfull
                // thus assume the first "../" means package escape and could be treated as "./". from the folder
                // hierarchy point of view
                // the simplest way is to just remove the first "../" and check again
                std::string checkPath(mdlPath.GetAuthoredPath().substr(3));
                resolvedMdlPath = UsdAssetConverter<TargetEngine>::ResolvePath(pxr::SdfAssetPath(checkPath), stage);
                // if we were still unable to resolve this to an existing file, we just cant't find the module where
                // it is expected to be
                if (resolvedMdlPath.IsEmpty()) return mi::base::Handle<const mi::neuraylib::IFunction_definition>();

                checkedRelativePath = checkedRelativePath.substr(3);
            }
            // now we know, that the requested mdl module exists in either path variants. To be able to open it with
            // the mdl sdk we need to translate the path into a module name. This transform will require that
            // the module name is relative to one of the search paths it could be found in.
            // to be able to do this we first need the absolute path to this resource, thus we combine the stage path
            // and the relative path to do this
            // as the resolved path may include a custum uri scheme the absolute local path is stored by the
            // asset resolver implementations in the assetInfo.
            std::filesystem::path realAbsolutePath;
            pxr::ArResolver& resolver = pxr::ArGetResolver();
            pxr::ArAssetInfo assetInfo = resolver.GetAssetInfo(resolvedMdlPath, resolvedMdlPath);
            std::string localPath;
            if (!assetInfo.resolverInfo.IsEmpty() && assetInfo.resolverInfo.IsHolding<std::string>())
            {
                localPath = assetInfo.resolverInfo.Get<std::string>();
                realAbsolutePath = std::filesystem::path(localPath).lexically_normal();
            }
            else
            {
                std::string absolutePath = stage->GetRootLayer()->ComputeAbsolutePath(checkedRelativePath);
                std::filesystem::path realPath =
                    std::filesystem::path(stage->GetRootLayer()->GetRealPath().c_str()).remove_filename();
                realAbsolutePath =
                    std::filesystem::absolute(realPath.string() + "/" + checkedRelativePath).lexically_normal();
            }
            // check the search path's this one is a child of and create the module name with this search path
            // as the anchor point
            mi::base::Handle<mi::neuraylib::IMdl_configuration> mdlConfig(
                neurayEntry->get_api_component<mi::neuraylib::IMdl_configuration>());
            for (int p = 0; p < mdlConfig->get_mdl_paths_length(); ++p)
            {
                std::filesystem::path searchPath =
                    std::filesystem::absolute(mdlConfig->get_mdl_path(p)->get_c_str()).lexically_normal();
                if (realAbsolutePath.string().rfind(searchPath.string(), 0) != 0) continue;
                // the searchPath is contained in the real absolute path we can use the part that matches as the anchor
                std::filesystem::path anchorPath = std::filesystem::relative(realAbsolutePath, searchPath);
                // now remove the file extension and replace all path seperators with "::"
                mdlPackageName =
                    std::regex_replace(anchorPath.replace_extension("").string(), std::regex(R"([/\\])"), "::");
                // the final package name need to be prefixed with "::"
                mdlPackageName = "::" + mdlPackageName;
                break;
            }
        }
        else
        {
            // if the asset path is an absolut path on the other hand the mdl sdk will look this asset up within it's
            // search paths. For this to work, the path is required to be translated into a mdl package name.
            std::filesystem::path path(mdlPath.GetAuthoredPath().c_str());
            try
            {
                // first remove the file extension and replace all path seperators with "::"
                mdlPackageName = std::regex_replace(path.replace_extension("").string(), std::regex(R"([/\\])"), "::");
                // the final package name need to be prefixed with "::"
                mdlPackageName = "::" + mdlPackageName;
            }
            catch (const std::regex_error& e)
            {
                // if the regex failes for whatever reason we are unable to build the package name and thus can't
                // access the material definition from the mdl module
                return mi::base::Handle<const mi::neuraylib::IFunction_definition>();
            }
        }

        // with the mdl module name in place we can use the import/export API to load and access the material
        // definition within
        mi::base::Handle<mi::neuraylib::IMdl_impexp_api> importExportApi(
            neurayEntry->get_api_component<mi::neuraylib::IMdl_impexp_api>());
        mi::base::Handle<mi::neuraylib::IMdl_execution_context> executionContext(
            mdl_factory()->create_execution_context());

        if (mi::Sint32 ret =
                importExportApi->load_module(mdl_transaction().get(), mdlPackageName.c_str(), executionContext.get());
            ret < 0)
        {
            // TODO: some logging or error message if loading the module failed?
            return mi::base::Handle<const mi::neuraylib::IFunction_definition>();
        }

        // access to the loaded module is done with its database name
        mi::base::Handle<const mi::IString> moduleDbName(mdl_factory()->get_db_module_name(mdlPackageName.c_str()));
        mi::base::Handle<const mi::neuraylib::IModule> module(
            mdl_transaction()->access<const mi::neuraylib::IModule>(moduleDbName->get_c_str()));

        // with access to th module we can check if it actually defines a material
        if (mi::Size materialCount = module->get_material_count(); materialCount <= 0)
        {
            return mi::base::Handle<const mi::neuraylib::IFunction_definition>();
        }

        // now lets pick the first defined material
        mi::base::Handle<const mi::neuraylib::IFunction_definition> material(
            mdl_transaction()->access<const mi::neuraylib::IFunction_definition>(module->get_material(0)));

        return material;
    }

    void SetMaterialChannelTexture(types::MaterialDescription<TextureType> material, types::MaterialChannel channel,
                                   const std::string& textureFilePath, const pxr::UsdShadeMaterial& usdMaterial)
    {
        if (textureFilePath.empty()) return;
        // if the texture asset path contains the UDIM placeholder, it indicates, that it uses texture
        // tiles. The texture tile ID is calculated based on the uv coordinates used. However, in this case
        // we need to find all available tile files and build a list of them containing the ID as well. This
        // will enable the target engine to properly handle uv tiling in their material generation
        if (textureFilePath.find("<UDIM>") != std::string::npos)
        {
            material.withTiles = true;
            material.channels.emplace(channel, GetTextureTiles(textureFilePath, usdMaterial));
        }
        else
        {
            types::TextureDescription<TextureType> texture;
            texture.filePath = pxr::SdfAssetPath(textureFilePath);
            material.channels.emplace(channel, texture);
        }
    }

    class pxr::GfVec3f GetColor(const std::string& parameterName, const pxr::UsdShadeConnectableAPI& shader,
                                mi::base::Handle<const mi::neuraylib::IExpression_list> mdlDefaults)
    {
        if (pxr::UsdShadeInput shaderInput = shader.GetInput(pxr::TfToken(parameterName)); shaderInput.IsDefined())
        {
            class pxr::GfVec3f color;
            if (shaderInput.Get(&color)) return color;
        }

        // extract the color value from mdl shader
        mi::Size defaultIndex = mdlDefaults->get_index(parameterName.c_str());
        mi::base::Handle<const mi::neuraylib::IExpression> defaultValue(mdlDefaults->get_expression(defaultIndex));
        mi::base::Handle<const mi::neuraylib::IExpression_constant> defaultConstant(
            defaultValue->get_interface<mi::neuraylib::IExpression_constant>());
        if (!defaultConstant)
        {
            defaultValue.reset();
            return {};
        }

        mi::base::Handle<const mi::neuraylib::IValue> constantValue(defaultConstant->get_value());
        if (constantValue->get_kind() != mi::neuraylib::IValue::VK_COLOR)
        {
            constantValue.reset();
            defaultConstant.reset();
            defaultValue.reset();
            return {};
        }
        mi::base::Handle<const mi::neuraylib::IValue_color> colorValue(
            constantValue->get_interface<mi::neuraylib::IValue_color>());
        if (!colorValue)
        {
            constantValue.reset();
            defaultConstant.reset();
            defaultValue.reset();
            return {};
        }

        return pxr::GfVec3f(colorValue->get_value(0)->get_value(), colorValue->get_value(1)->get_value(),
                            colorValue->get_value(2)->get_value());
    }

    std::string GetTextureFilePath(const std::string& parameterName, const pxr::UsdShadeConnectableAPI& shader,
                                   mi::base::Handle<const mi::neuraylib::IExpression_list> mdlDefaults)
    {
        if (pxr::UsdShadeInput shaderInput = shader.GetInput(pxr::TfToken(parameterName)); shaderInput.IsDefined())
        {
            // the material parameter value is provided by the usd shader prim
            pxr::SdfAssetPath assetPath;
            if (shaderInput.Get(&assetPath))
            {
                return assetPath.GetAssetPath();
            }
        }

        // The texture file path is not provided by the usd shader prim. We try to get it from the mdl defaults
        // the material parameter value is provided by the mdl definition
        mi::Size defaultIndex = mdlDefaults->get_index(parameterName.c_str());
        mi::base::Handle<const mi::neuraylib::IExpression> defaultValue(mdlDefaults->get_expression(defaultIndex));
        // check if the default value is a constant and not a calculated one. As we do not convert any shader nodes, we
        // can only consider constant default values.
        mi::base::Handle<const mi::neuraylib::IExpression_constant> defaultConstant(
            defaultValue->get_interface<mi::neuraylib::IExpression_constant>());
        if (!defaultConstant)
        {
            defaultValue.reset();
            return std::string();
        }

        // Extracting the actual value of the default constant we need to switch on the respective value kind
        mi::base::Handle<const mi::neuraylib::IValue> constantValue(defaultConstant->get_value());
        if (constantValue->get_kind() != mi::neuraylib::IValue::VK_TEXTURE)
        {
            constantValue.reset();
            defaultConstant.reset();
            defaultValue.reset();
            return std::string();
        }

        // extract the texture data and the path to the actual texture file to be able to
        // construct the TextureDescription
        mi::base::Handle<const mi::neuraylib::IValue_texture> textureValue(
            constantValue->get_interface<mi::neuraylib::IValue_texture>());
        if (textureValue->get_file_path() == nullptr)
        {
            textureValue.reset();
            constantValue.reset();
            defaultConstant.reset();
            defaultValue.reset();
            return std::string();
        }

        mi::base::Handle<const mi::neuraylib::ITexture> texture(
            mdl_transaction()->access<const mi::neuraylib::ITexture>(textureValue->get_value()));
        if (!texture)
        {
            textureValue.reset();
            constantValue.reset();
            defaultConstant.reset();
            defaultValue.reset();
            return std::string();
        }

        mi::base::Handle<const mi::neuraylib::IImage> image(
            mdl_transaction()->access<const mi::neuraylib::IImage>(texture->get_image()));
        if (!image)
        {
            texture.reset();
            textureValue.reset();
            constantValue.reset();
            defaultConstant.reset();
            defaultValue.reset();
            return std::string();
        }

        // from the image we can extract the texture asset file path
        std::string textureFilePath(image->get_filename(0, 0));

        image.reset();
        texture.reset();
        textureValue.reset();
        constantValue.reset();
        defaultConstant.reset();
        defaultValue.reset();

        return textureFilePath;
    }

    std::vector<types::TextureDescription<TextureType>> GetTextureTiles(const std::string& textureFilePath,
                                                                        const pxr::UsdShadeMaterial& usdMaterial)
    {
        std::vector<types::TextureDescription<TextureType>> textureTiles;

        // lookup all files that match the <UDIM> pattern in the directory the file is pointing to
        // to do so we need to resolve the path first. As the asset resolver checks if the file exists
        // we nee do replace the <UDIM> with a valid value. To properly resolve the path we use the
        // smallest tile ID that should always exist as a starting point
        std::string checkFileName = std::regex_replace(textureFilePath, std::regex(R"(<UDIM>)"), "1001");
        pxr::ArResolvedPath resolvedPath = UsdAssetConverter<TargetEngine>::ResolvePath(
            pxr::SdfAssetPath(checkFileName), usdMaterial.GetPrim().GetStage());
        std::filesystem::path checkTexturePathFile = std::filesystem::path(resolvedPath.GetPathString());
        std::filesystem::path texturePath = checkTexturePathFile.parent_path();
        if (!std::filesystem::exists(texturePath) || !std::filesystem::is_directory(texturePath))
        {
            // well there does not seem to exist such a directory
            return {};
        }
        std::filesystem::path textureFile = std::filesystem::path(textureFilePath).filename();
        // use a regex with capture group to find the right files and their tile id
        std::string textureFilePattern =
            std::regex_replace(textureFile.string(), std::regex(R"(<UDIM>)"), R"((\d{4}))");
        std::regex textureFileRegex(textureFilePattern);
        for (const auto& entry: std::filesystem::directory_iterator(texturePath))
        {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            std::smatch match;

            // check if this file matches the expected pattern and also extracting the tile identifier
            if (std::regex_match(filename, match, textureFileRegex))
            {
                // Extract UDIM from first capture group
                if (match.size() >= 2)
                {
                    types::TextureDescription<TextureType> tile;
                    tile.filePath = pxr::SdfAssetPath(textureFilePath, entry.path().string());
                    tile.tileId = match[1].str();
                    textureTiles.push_back(tile);
                }
            }
        }

        return textureTiles;
    }
};
} // namespace converter
} // namespace idtxflow
#endif