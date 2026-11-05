//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef IDTX_GENERATED_RESTDATASOURCE_H
#define IDTX_GENERATED_RESTDATASOURCE_H

/// \file IDTX/restDatasource.h

#include "pxr/pxr.h"
#include "./api.h"
#include "./datasource.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "./tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// RESTDATASOURCE                                                             //
// -------------------------------------------------------------------------- //

/// \class IDTXRestDatasource
///
/// This is a REST data source that calls the provided REST endoint and returns the JSON response.
/// 
/// 
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref IDTXTokens.
/// So to set an attribute to the value "rightHanded", use IDTXTokens->rightHanded
/// as the value.
///
class IDTXRestDatasource : public IDTXDatasource
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::ConcreteTyped;

    /// Construct a IDTXRestDatasource on UsdPrim \p prim .
    /// Equivalent to IDTXRestDatasource::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit IDTXRestDatasource(const UsdPrim& prim=UsdPrim())
        : IDTXDatasource(prim)
    {
    }

    /// Construct a IDTXRestDatasource on the prim held by \p schemaObj .
    /// Should be preferred over IDTXRestDatasource(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit IDTXRestDatasource(const UsdSchemaBase& schemaObj)
        : IDTXDatasource(schemaObj)
    {
    }

    /// Destructor.
    IDTX_API
    virtual ~IDTXRestDatasource();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    IDTX_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a IDTXRestDatasource holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// IDTXRestDatasource(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    IDTX_API
    static IDTXRestDatasource
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Attempt to ensure a \a UsdPrim adhering to this schema at \p path
    /// is defined (according to UsdPrim::IsDefined()) on this stage.
    ///
    /// If a prim adhering to this schema at \p path is already defined on this
    /// stage, return that prim.  Otherwise author an \a SdfPrimSpec with
    /// \a specifier == \a SdfSpecifierDef and this schema's prim type name for
    /// the prim at \p path at the current EditTarget.  Author \a SdfPrimSpec s
    /// with \p specifier == \a SdfSpecifierDef and empty typeName at the
    /// current EditTarget for any nonexistent, or existing but not \a Defined
    /// ancestors.
    ///
    /// The given \a path must be an absolute prim path that does not contain
    /// any variant selections.
    ///
    /// If it is impossible to author any of the necessary PrimSpecs, (for
    /// example, in case \a path cannot map to the current UsdEditTarget's
    /// namespace) issue an error and return an invalid \a UsdPrim.
    ///
    /// Note that this method may return a defined prim whose typeName does not
    /// specify this schema class, in case a stronger typeName opinion overrides
    /// the opinion at the current EditTarget.
    ///
    IDTX_API
    static IDTXRestDatasource
    Define(const UsdStagePtr &stage, const SdfPath &path);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    IDTX_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    IDTX_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    IDTX_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // ENDPOINT 
    // --------------------------------------------------------------------- //
    /// The base URL of the REST API endpoint
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `string endpoint = ""` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    IDTX_API
    UsdAttribute GetEndpointAttr() const;

    /// See GetEndpointAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateEndpointAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // QUERY 
    // --------------------------------------------------------------------- //
    /// The query to append to the endpoint URL after the path. Value is url encoded
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `string query = ""` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    IDTX_API
    UsdAttribute GetQueryAttr() const;

    /// See GetQueryAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateQueryAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // METHOD 
    // --------------------------------------------------------------------- //
    /// HTTP Method to be used for the call to the endpoint. Allowed: GET, POST
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `token method = "GET"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref IDTXTokens "Allowed Values" | GET, POST |
    IDTX_API
    UsdAttribute GetMethodAttr() const;

    /// See GetMethodAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateMethodAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // AUTHORIZATION 
    // --------------------------------------------------------------------- //
    /// The authorization header value passed to the request. Usually 'Bearer <TOKEN>'
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `string authorization` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    IDTX_API
    UsdAttribute GetAuthorizationAttr() const;

    /// See GetAuthorizationAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateAuthorizationAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // JSONBODY 
    // --------------------------------------------------------------------- //
    /// The JSON body to be send to the endpoint, when the method is set to POST
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `string jsonBody = "{}"` |
    /// | C++ Type | std::string |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->String |
    IDTX_API
    UsdAttribute GetJsonBodyAttr() const;

    /// See GetJsonBodyAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateJsonBodyAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // INTERVAL 
    // --------------------------------------------------------------------- //
    /// The update interval in seconds the REST datasource shall call it's endpoint and author the response into \sa outputs:data attribute in the current usd stage this prim is authored. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float interval = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    IDTX_API
    UsdAttribute GetIntervalAttr() const;

    /// See GetIntervalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateIntervalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
