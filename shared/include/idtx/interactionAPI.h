//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef IDTX_GENERATED_INTERACTIONAPI_H
#define IDTX_GENERATED_INTERACTIONAPI_H

/// \file IDTX/interactionAPI.h

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
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
// INTERACTIONAPI                                                             //
// -------------------------------------------------------------------------- //

/// \class IDTXInteractionAPI
///
/// Interaction-related metadata attachable to prims.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref IDTXTokens.
/// So to set an attribute to the value "rightHanded", use IDTXTokens->rightHanded
/// as the value.
///
class IDTXInteractionAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a IDTXInteractionAPI on UsdPrim \p prim .
    /// Equivalent to IDTXInteractionAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit IDTXInteractionAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a IDTXInteractionAPI on the prim held by \p schemaObj .
    /// Should be preferred over IDTXInteractionAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit IDTXInteractionAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    IDTX_API
    virtual ~IDTXInteractionAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    IDTX_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a IDTXInteractionAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// IDTXInteractionAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    IDTX_API
    static IDTXInteractionAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);


    /// Returns true if this <b>single-apply</b> API schema can be applied to 
    /// the given \p prim. If this schema can not be a applied to the prim, 
    /// this returns false and, if provided, populates \p whyNot with the 
    /// reason it can not be applied.
    /// 
    /// Note that if CanApply returns false, that does not necessarily imply
    /// that calling Apply will fail. Callers are expected to call CanApply
    /// before calling Apply if they want to ensure that it is valid to 
    /// apply a schema.
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    IDTX_API
    static bool 
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "InteractionAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid IDTXInteractionAPI object is returned upon success. 
    /// An invalid (or empty) IDTXInteractionAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    IDTX_API
    static IDTXInteractionAPI 
    Apply(const UsdPrim &prim);

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
    // INTERACTIONENABLED 
    // --------------------------------------------------------------------- //
    /// Define if colliders participate in interaction.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `bool interaction:enabled = 1` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    IDTX_API
    UsdAttribute GetInteractionEnabledAttr() const;

    /// See GetInteractionEnabledAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateInteractionEnabledAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // INTERACTIONHIGHLIGHTABLE 
    // --------------------------------------------------------------------- //
    /// Define if the asset shall display a highlight effect when selected. Actual implementation needs to be done on the engine side.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `bool interaction:highlightable = 1` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    IDTX_API
    UsdAttribute GetInteractionHighlightableAttr() const;

    /// See GetInteractionHighlightableAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateInteractionHighlightableAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // INTERACTIONHIGHLIGHTCOLOR 
    // --------------------------------------------------------------------- //
    /// Color for the highlight effect.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f interaction:highlightColor = (1, 0, 0)` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    IDTX_API
    UsdAttribute GetInteractionHighlightColorAttr() const;

    /// See GetInteractionHighlightColorAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateInteractionHighlightColorAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // INTERACTIONIDENTIFIER 
    // --------------------------------------------------------------------- //
    /// Used to link the asset to a specific interaction behavior in the engine. E.g. passing to the implemented interaction handler methods.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token interaction:identifier = "NO_VALUE"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    IDTX_API
    UsdAttribute GetInteractionIdentifierAttr() const;

    /// See GetInteractionIdentifierAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateInteractionIdentifierAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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
