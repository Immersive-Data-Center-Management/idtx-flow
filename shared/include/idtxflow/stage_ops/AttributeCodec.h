#pragma once

/**
 * @file AttributeCodec.h
 * @brief Convert plain typed attributes to/from OpenUSD values and author defaults.
 *
 * Validates batches against USD types and properties, anchors asset values and
 * writes prepared defaults to a layer. Preserves type roles and array identity
 * during conversion between plain attribute records and OpenUSD values.
 */

#include <idtxflow/net/model/AttributeTypes.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/usd/stage.h>

namespace idtxflow::stage_ops
{
/// Result of interpreting typed components as an OpenUSD value.
enum class DecodeStatus
{
    Ok,
    UnsupportedType,
    InvalidValue
};

/// Stateless conversion between typed attribute components and OpenUSD values.
class AttributeCodec
{
  public:
    /// Decode scalar/array components. Reject finite narrowing overflow; retain NaN/inf.
    static DecodeStatus DecodeValue(const pxr::SdfValueTypeName& type, const net::model::AttributeValue& in,
                                    pxr::VtValue& out);
    /// Encode using the supplied USD type so role and array identity are retained.
    static bool EncodeValue(const pxr::SdfValueTypeName& type, const pxr::VtValue& in, net::model::AttributeValue& out);
};

/// A validated, anchored default ready for Sdf authoring without composition queries.
struct PreparedAttribute
{
    std::string name;
    pxr::SdfValueTypeName type;
    pxr::VtValue value;
    bool custom = true;
    pxr::SdfVariability variability = pxr::SdfVariabilityVarying;
};

/// Validate an entire batch before mutation; type_hint describes a not-yet-created built-in.
bool prepare_attributes(const pxr::UsdStageRefPtr& stage, const pxr::SdfLayerHandle& target, const pxr::SdfPath& path,
                        const std::vector<net::model::PrimAttribute>& attributes, std::vector<PreparedAttribute>& out,
                        std::string& error, const std::string& type_hint = {});
/// Write validated defaults, reusing compatible specs. Caller owns the Sdf change block.
bool author_attributes(const pxr::SdfLayerHandle& layer, const pxr::SdfPath& path,
                       const std::vector<PreparedAttribute>& attributes);
} // namespace idtxflow::stage_ops
