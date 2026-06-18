#pragma once
/**
 * @file ExecComputeResult.h
 * @brief
 * 
 **/

#include <pxr/usd/sdf/path.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>

/**
 * Holds a single computed value together with provenance information (which prim and computation produced it).
 **/
struct ExecComputeResult
{
    // The prim path that owns the computation (e.g. /World/Cube).
    pxr::SdfPath primPath;
    // The attribute name of the prim the computation was requested for (e.g. primvars:displayColor)
    std::string primAttribute;
    // The computation token (e.g. outputs:jsonValue:double).
    pxr::TfToken computation;
    // The computed value.
    pxr::VtValue value;
    // Index in the original ExecUsdValueKey vector passed to BuildRequest().
    int requestIndex = -1;
};