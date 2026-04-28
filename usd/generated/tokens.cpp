//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

IDTXTokensType::IDTXTokensType() :
    Box("Box", TfToken::Immortal),
    Capsule("Capsule", TfToken::Immortal),
    collisionShape("collision:shape", TfToken::Immortal),
    collisionType("collision:type", TfToken::Immortal),
    colorMod("colorMod", TfToken::Immortal),
    Convex("Convex", TfToken::Immortal),
    Cylinder("Cylinder", TfToken::Immortal),
    fixedValue("fixedValue", TfToken::Immortal),
    Grab("Grab", TfToken::Immortal),
    guide("guide", TfToken::Immortal),
    interactionEnabled("interaction:enabled", TfToken::Immortal),
    interactionHighlightable("interaction:highlightable", TfToken::Immortal),
    interactionHighlightColor("interaction:highlightColor", TfToken::Immortal),
    interactionIdentifier("interaction:identifier", TfToken::Immortal),
    interactionInteractionTypes("interaction:interactionTypes", TfToken::Immortal),
    invisible("invisible", TfToken::Immortal),
    labelName("labelName", TfToken::Immortal),
    NO_VALUE("NO_VALUE", TfToken::Immortal),
    physicsCollider("physics:collider", TfToken::Immortal),
    physicsColliderQuerry("physics:collider:querry", TfToken::Immortal),
    purpose("purpose", TfToken::Immortal),
    Rigidbody("Rigidbody", TfToken::Immortal),
    Select("Select", TfToken::Immortal),
    Sphere("Sphere", TfToken::Immortal),
    Static("Static", TfToken::Immortal),
    visibility("visibility", TfToken::Immortal),
    CollisionAPI("CollisionAPI", TfToken::Immortal),
    CollisionSetAPI("CollisionSetAPI", TfToken::Immortal),
    InteractionAPI("InteractionAPI", TfToken::Immortal),
    StaticDataWidget("StaticDataWidget", TfToken::Immortal),
    allTokens({
        Box,
        Capsule,
        collisionShape,
        collisionType,
        colorMod,
        Convex,
        Cylinder,
        fixedValue,
        Grab,
        guide,
        interactionEnabled,
        interactionHighlightable,
        interactionHighlightColor,
        interactionIdentifier,
        interactionInteractionTypes,
        invisible,
        labelName,
        NO_VALUE,
        physicsCollider,
        physicsColliderQuerry,
        purpose,
        Rigidbody,
        Select,
        Sphere,
        Static,
        visibility,
        CollisionAPI,
        CollisionSetAPI,
        InteractionAPI,
        StaticDataWidget
    })
{
}

TfStaticData<IDTXTokensType> IDTXTokens;

PXR_NAMESPACE_CLOSE_SCOPE
