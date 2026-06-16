//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./compute_ColorFromFloat.h"
#include "pxr/usd/usd/schemaBase.h"

#include "pxr/usd/sdf/primSpec.h"

#include "pxr/usd/usd/pyConversions.h"
#include "pxr/base/tf/pyContainerConversions.h"
#include "pxr/base/tf/pyResultConversions.h"
#include "pxr/base/tf/pyUtils.h"
#include "pxr/base/tf/wrapTypeHelpers.h"

#include "pxr/external/boost/python.hpp"

#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace pxr_boost::python;

namespace {

#define WRAP_CUSTOM                                                     \
    template <class Cls> static void _CustomWrapCode(Cls &_class)

// fwd decl.
WRAP_CUSTOM;

        
static UsdAttribute
_CreateBoundariesAttr(IDTXCompute_ColorFromFloat &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateBoundariesAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->FloatArray), writeSparsely);
}
        
static UsdAttribute
_CreateColorsAttr(IDTXCompute_ColorFromFloat &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateColorsAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Color3fArray), writeSparsely);
}
        
static UsdAttribute
_CreateInputsValueAttr(IDTXCompute_ColorFromFloat &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateInputsValueAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}
        
static UsdAttribute
_CreateOutputsResultAttr(IDTXCompute_ColorFromFloat &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateOutputsResultAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Color3f), writeSparsely);
}

static std::string
_Repr(const IDTXCompute_ColorFromFloat &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "IDTX.Compute_ColorFromFloat(%s)",
        primRepr.c_str());
}

} // anonymous namespace

void wrapIDTXCompute_ColorFromFloat()
{
    typedef IDTXCompute_ColorFromFloat This;

    class_<This, bases<UsdTyped> >
        cls("Compute_ColorFromFloat");

    cls
        .def(init<UsdPrim>(arg("prim")))
        .def(init<UsdSchemaBase const&>(arg("schemaObj")))
        .def(TfTypePythonClass())

        .def("Get", &This::Get, (arg("stage"), arg("path")))
        .staticmethod("Get")

        .def("Define", &This::Define, (arg("stage"), arg("path")))
        .staticmethod("Define")

        .def("GetSchemaAttributeNames",
             &This::GetSchemaAttributeNames,
             arg("includeInherited")=true,
             return_value_policy<TfPySequenceToList>())
        .staticmethod("GetSchemaAttributeNames")

        .def("_GetStaticTfType", (TfType const &(*)()) TfType::Find<This>,
             return_value_policy<return_by_value>())
        .staticmethod("_GetStaticTfType")

        .def(!self)

        
        .def("GetBoundariesAttr",
             &This::GetBoundariesAttr)
        .def("CreateBoundariesAttr",
             &_CreateBoundariesAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetColorsAttr",
             &This::GetColorsAttr)
        .def("CreateColorsAttr",
             &_CreateColorsAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetInputsValueAttr",
             &This::GetInputsValueAttr)
        .def("CreateInputsValueAttr",
             &_CreateInputsValueAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetOutputsResultAttr",
             &This::GetOutputsResultAttr)
        .def("CreateOutputsResultAttr",
             &_CreateOutputsResultAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))

        .def("__repr__", ::_Repr)
    ;

    _CustomWrapCode(cls);
}

// ===================================================================== //
// Feel free to add custom code below this line, it will be preserved by 
// the code generator.  The entry point for your custom code should look
// minimally like the following:
//
// WRAP_CUSTOM {
//     _class
//         .def("MyCustomMethod", ...)
//     ;
// }
//
// Of course any other ancillary or support code may be provided.
// 
// Just remember to wrap code in the appropriate delimiters:
// 'namespace {', '}'.
//
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--

namespace {

WRAP_CUSTOM {
}

}
