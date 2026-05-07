//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// GENERATED FILE.  DO NOT EDIT.
#include "pxr/external/boost/python/class.hpp"
#include "./tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

#define _ADD_TOKEN(cls, name) \
    cls.add_static_property(#name, +[]() { return IDTXTokens->name.GetString(); });

void wrapIDTXTokens()
{
    pxr_boost::python::class_<IDTXTokensType, pxr_boost::python::noncopyable>
        cls("Tokens", pxr_boost::python::no_init);
    _ADD_TOKEN(cls, Box);
    _ADD_TOKEN(cls, Capsule);
    _ADD_TOKEN(cls, Collide);
    _ADD_TOKEN(cls, collisionInteractionTypes);
    _ADD_TOKEN(cls, collisionShape);
    _ADD_TOKEN(cls, collisionType);
    _ADD_TOKEN(cls, Convex);
    _ADD_TOKEN(cls, Cylinder);
    _ADD_TOKEN(cls, Grab);
    _ADD_TOKEN(cls, guide);
    _ADD_TOKEN(cls, interactionEnabled);
    _ADD_TOKEN(cls, interactionHighlightable);
    _ADD_TOKEN(cls, interactionHighlightColor);
    _ADD_TOKEN(cls, interactionIdentifier);
    _ADD_TOKEN(cls, invisible);
    _ADD_TOKEN(cls, NO_VALUE);
    _ADD_TOKEN(cls, physicsCollider);
    _ADD_TOKEN(cls, physicsColliderQuerry);
    _ADD_TOKEN(cls, purpose);
    _ADD_TOKEN(cls, Rigidbody);
    _ADD_TOKEN(cls, Select);
    _ADD_TOKEN(cls, Sphere);
    _ADD_TOKEN(cls, Static);
    _ADD_TOKEN(cls, visibility);
    _ADD_TOKEN(cls, CollisionAPI);
    _ADD_TOKEN(cls, CollisionSetAPI);
    _ADD_TOKEN(cls, InteractionAPI);
}
