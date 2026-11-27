#pragma once

/**
 * @file AttributeCodec.h
 * @brief Convert typed attribute dictionaries at the Godot binding boundary.
 *
 * Produces plain attribute records without authoring a stage or sending requests.
 * USD type/component validation is shared with the stage attribute codec.
 */

#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/array.hpp>
#include <idtxflow/net/model/AttributeTypes.h>

namespace idtxflow::collab::attributes
{
/// Decode an Array of typed attribute dictionaries. On failure, out is empty and
/// error identifies the invalid entry. Unsigned components use decimal Strings.
bool decode_attributes(const godot::Variant& input, std::vector<net::model::PrimAttribute>& out, std::string& error);
/// Package owned typed defaults using the same lanes accepted by decode_attributes.
godot::Array encode_attributes(const std::vector<net::model::PrimAttribute>& attributes);
} // namespace idtxflow::collab::attributes
