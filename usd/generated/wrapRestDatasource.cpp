//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./restDatasource.h"
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
_CreateEndpointAttr(IDTXRestDatasource &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateEndpointAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateQueryAttr(IDTXRestDatasource &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateQueryAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateMethodAttr(IDTXRestDatasource &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateMethodAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Token), writeSparsely);
}
        
static UsdAttribute
_CreateAuthorizationAttr(IDTXRestDatasource &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateAuthorizationAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateJsonBodyAttr(IDTXRestDatasource &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateJsonBodyAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->String), writeSparsely);
}
        
static UsdAttribute
_CreateIntervalAttr(IDTXRestDatasource &self,
                                      object defaultVal, bool writeSparsely) {
    return self.CreateIntervalAttr(
        UsdPythonToSdfType(defaultVal, SdfValueTypeNames->Float), writeSparsely);
}

static std::string
_Repr(const IDTXRestDatasource &self)
{
    std::string primRepr = TfPyRepr(self.GetPrim());
    return TfStringPrintf(
        "IDTX.RestDatasource(%s)",
        primRepr.c_str());
}

} // anonymous namespace

void wrapIDTXRestDatasource()
{
    typedef IDTXRestDatasource This;

    class_<This, bases<IDTXDatasource> >
        cls("RestDatasource");

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

        
        .def("GetEndpointAttr",
             &This::GetEndpointAttr)
        .def("CreateEndpointAttr",
             &_CreateEndpointAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetQueryAttr",
             &This::GetQueryAttr)
        .def("CreateQueryAttr",
             &_CreateQueryAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetMethodAttr",
             &This::GetMethodAttr)
        .def("CreateMethodAttr",
             &_CreateMethodAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetAuthorizationAttr",
             &This::GetAuthorizationAttr)
        .def("CreateAuthorizationAttr",
             &_CreateAuthorizationAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetJsonBodyAttr",
             &This::GetJsonBodyAttr)
        .def("CreateJsonBodyAttr",
             &_CreateJsonBodyAttr,
             (arg("defaultValue")=object(),
              arg("writeSparsely")=false))
        
        .def("GetIntervalAttr",
             &This::GetIntervalAttr)
        .def("CreateIntervalAttr",
             &_CreateIntervalAttr,
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
