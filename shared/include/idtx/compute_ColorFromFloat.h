//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef IDTX_GENERATED_COMPUTE_COLORFROMFLOAT_H
#define IDTX_GENERATED_COMPUTE_COLORFROMFLOAT_H

/// \file IDTX/compute_ColorFromFloat.h

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/usd/usd/typed.h"
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
// COMPUTE_COLORFROMFLOAT                                                     //
// -------------------------------------------------------------------------- //

/// \class IDTXCompute_ColorFromFloat
///
///
class IDTXCompute_ColorFromFloat : public UsdTyped
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::ConcreteTyped;

    /// Construct a IDTXCompute_ColorFromFloat on UsdPrim \p prim .
    /// Equivalent to IDTXCompute_ColorFromFloat::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit IDTXCompute_ColorFromFloat(const UsdPrim& prim=UsdPrim())
        : UsdTyped(prim)
    {
    }

    /// Construct a IDTXCompute_ColorFromFloat on the prim held by \p schemaObj .
    /// Should be preferred over IDTXCompute_ColorFromFloat(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit IDTXCompute_ColorFromFloat(const UsdSchemaBase& schemaObj)
        : UsdTyped(schemaObj)
    {
    }

    /// Destructor.
    IDTX_API
    virtual ~IDTXCompute_ColorFromFloat();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    IDTX_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a IDTXCompute_ColorFromFloat holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// IDTXCompute_ColorFromFloat(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    IDTX_API
    static IDTXCompute_ColorFromFloat
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
    static IDTXCompute_ColorFromFloat
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
    // BOUNDARIES 
    // --------------------------------------------------------------------- //
    /// The array stored the upper bounds for the color to be choosen based on the input value. It should always have 1 entry less than the \sa colors array.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float[] boundaries` |
    /// | C++ Type | VtArray<float> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->FloatArray |
    IDTX_API
    UsdAttribute GetBoundariesAttr() const;

    /// See GetBoundariesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateBoundariesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // COLORS 
    // --------------------------------------------------------------------- //
    /// The arry of colors to choose from, when the input value falls into the corresponing \sa boundaries bucket.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f[] colors` |
    /// | C++ Type | VtArray<GfVec3f> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3fArray |
    IDTX_API
    UsdAttribute GetColorsAttr() const;

    /// See GetColorsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateColorsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // INPUTSVALUE 
    // --------------------------------------------------------------------- //
    /// 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:value = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    IDTX_API
    UsdAttribute GetInputsValueAttr() const;

    /// See GetInputsValueAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateInputsValueAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // OUTPUTSRESULT 
    // --------------------------------------------------------------------- //
    /// 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f outputs:result` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    IDTX_API
    UsdAttribute GetOutputsResultAttr() const;

    /// See GetOutputsResultAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    IDTX_API
    UsdAttribute CreateOutputsResultAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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
