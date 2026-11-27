#pragma once

/**
 * @file AttributeCodec.h
 * @brief Decode typed attribute dictionaries at the Godot binding boundary.
 *
 * Produces plain attribute records without authoring a stage or sending requests.
 * USD type/component validation is shared with the stage attribute codec.
 */

#include <godot_cpp/variant/variant.hpp>
#include <idtxflow/net/model/AttributeTypes.h>

namespace idtxflow::collab::attributes
{
/// Decode an Array of typed attribute dictionaries. On failure, out is empty and
/// error identifies the invalid entry. Unsigned components use decimal Strings.
bool decode_attributes(const godot::Variant& input, std::vector<net::model::PrimAttribute>& out, std::string& error);
} // namespace idtxflow::collab::attributes
