//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef IDTX_TOKENS_H
#define IDTX_TOKENS_H

/// \file IDTX/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// 
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
// 
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE


/// \class IDTXTokensType
///
/// \link IDTXTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// IDTXTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use IDTXTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(IDTXTokens->Box);
/// \endcode
struct IDTXTokensType {
    IDTX_API IDTXTokensType();
    /// \brief "Box"
    /// 
    /// Fallback value for IDTXCollisionAPI::GetCollisionShapeAttr()
    const TfToken Box;
    /// \brief "Capsule"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionShapeAttr()
    const TfToken Capsule;
    /// \brief "Collide"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionInteractionTypesAttr()
    const TfToken Collide;
    /// \brief "collision:interactionTypes"
    /// 
    /// IDTXCollisionAPI
    const TfToken collisionInteractionTypes;
    /// \brief "collision:shape"
    /// 
    /// IDTXCollisionAPI
    const TfToken collisionShape;
    /// \brief "collision:type"
    /// 
    /// IDTXCollisionAPI
    const TfToken collisionType;
    /// \brief "Convex"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionShapeAttr()
    const TfToken Convex;
    /// \brief "Cylinder"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionShapeAttr()
    const TfToken Cylinder;
    /// \brief "Grab"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionInteractionTypesAttr()
    const TfToken Grab;
    /// \brief "guide"
    /// 
    /// Fallback value for IDTXCollisionAPI schema attribute purpose
    const TfToken guide;
    /// \brief "interaction:enabled"
    /// 
    /// IDTXInteractionAPI
    const TfToken interactionEnabled;
    /// \brief "interaction:highlightable"
    /// 
    /// IDTXInteractionAPI
    const TfToken interactionHighlightable;
    /// \brief "interaction:highlightColor"
    /// 
    /// IDTXInteractionAPI
    const TfToken interactionHighlightColor;
    /// \brief "interaction:identifier"
    /// 
    /// IDTXInteractionAPI
    const TfToken interactionIdentifier;
    /// \brief "invisible"
    /// 
    /// Fallback value for IDTXCollisionAPI schema attribute visibility
    const TfToken invisible;
    /// \brief "NO_VALUE"
    /// 
    /// Fallback value for IDTXInteractionAPI::GetInteractionIdentifierAttr()
    const TfToken NO_VALUE;
    /// \brief "physics:collider"
    /// 
    /// IDTXCollisionSetAPI
    const TfToken physicsCollider;
    /// \brief "physics:collider:querry"
    /// 
    /// IDTXCollisionSetAPI
    const TfToken physicsColliderQuerry;
    /// \brief "purpose"
    /// 
    /// IDTXCollisionAPI
    const TfToken purpose;
    /// \brief "Rigidbody"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionTypeAttr()
    const TfToken Rigidbody;
    /// \brief "Select"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionInteractionTypesAttr()
    const TfToken Select;
    /// \brief "Sphere"
    /// 
    /// Possible value for IDTXCollisionAPI::GetCollisionShapeAttr()
    const TfToken Sphere;
    /// \brief "Static"
    /// 
    /// Fallback value for IDTXCollisionAPI::GetCollisionTypeAttr()
    const TfToken Static;
    /// \brief "visibility"
    /// 
    /// IDTXCollisionAPI
    const TfToken visibility;
    /// \brief "CollisionAPI"
    /// 
    /// Schema identifer and family for IDTXCollisionAPI
    const TfToken CollisionAPI;
    /// \brief "CollisionSetAPI"
    /// 
    /// Schema identifer and family for IDTXCollisionSetAPI
    const TfToken CollisionSetAPI;
    /// \brief "InteractionAPI"
    /// 
    /// Schema identifer and family for IDTXInteractionAPI
    const TfToken InteractionAPI;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var IDTXTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa IDTXTokensType
extern IDTX_API TfStaticData<IDTXTokensType> IDTXTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
