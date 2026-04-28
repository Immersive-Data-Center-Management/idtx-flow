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
    _ADD_TOKEN(cls, double_);
    _ADD_TOKEN(cls, float_);
    _ADD_TOKEN(cls, inputsJsonData);
    _ADD_TOKEN(cls, inputsJsonPath);
    _ADD_TOKEN(cls, inputsJsonValueType);
    _ADD_TOKEN(cls, none);
    _ADD_TOKEN(cls, outputsJsonValueDouble);
    _ADD_TOKEN(cls, outputsJsonValueFloat);
    _ADD_TOKEN(cls, outputsJsonValueString);
    _ADD_TOKEN(cls, string);
    _ADD_TOKEN(cls, Compute_ValueFromJson);
}
