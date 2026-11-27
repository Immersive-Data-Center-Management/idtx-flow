#pragma once

/**
 * @file AttributeTypes.h
 * @brief Plain typed attribute records shared by structural requests, edits and USD conversion.
 *
 * Preserve scalar type roles, array identity and signed/unsigned value ranges
 * as plain data, independent of rendering, serialization and USD runtime objects.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace idtxflow::net::model
{
/// Typed default-value components. Array identity is independent of component count.
struct AttributeValue
{
    bool is_array = false;
    std::vector<double> doubles;
    std::vector<int64_t> ints;
    std::vector<uint64_t> uints;
    std::vector<bool> bools;
    std::vector<std::string> strings;
};

/// One default attribute; type_name is the scalar USD type token, including its role.
struct PrimAttribute
{
    std::string name;
    std::string type_name;
    AttributeValue value;
};
} // namespace idtxflow::net::model
