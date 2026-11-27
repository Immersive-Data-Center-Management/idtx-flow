#include "AttributeCodec.h"

#include <charconv>
#include <set>

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/schema.h>
#include <idtxflow/stage_ops/AttributeCodec.h>

namespace idtxflow::collab::attributes
{
namespace
{
using godot::Array;
using godot::Dictionary;
using godot::String;
using godot::Variant;

std::string utf8(const String& value)
{
    const auto bytes = value.utf8();
    return {bytes.get_data(), static_cast<size_t>(bytes.length())};
}

template <typename T, typename Decode>
bool read_lane(const Dictionary& value, const char* name, std::vector<T>& out, std::string& error, Decode decode)
{
    if (!value.has(name)) return true;
    const Variant input = value[name];
    if (input.get_type() != Variant::ARRAY)
    {
        error = std::string(name) + " must be an Array";
        return false;
    }
    const Array elements = input;
    out.reserve(elements.size());
    for (int64_t i = 0; i < elements.size(); ++i)
    {
        T component{};
        if (!decode(elements[i], component))
        {
            error = std::string(name) + "[" + std::to_string(i) + "] has an invalid component type or range";
            return false;
        }
        out.push_back(std::move(component));
    }
    return true;
}

bool read_double(const Variant& input, double& out)
{
    if (input.get_type() == Variant::FLOAT)
    {
        out = static_cast<double>(input);
        return true;
    }
    if (input.get_type() != Variant::INT) return false;
    const int64_t integer = input;
    // Larger integers belong in ints/uints; do not silently round them into a double.
    constexpr int64_t exact_limit = int64_t{1} << 53;
    if (integer < -exact_limit || integer > exact_limit) return false;
    out = static_cast<double>(integer);
    return true;
}

bool read_uint(const Variant& input, uint64_t& out)
{
    if (input.get_type() != Variant::STRING) return false;
    const std::string text = utf8(input);
    if (text.empty() || text.front() < '0' || text.front() > '9') return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool read_attribute(const Variant& input, net::model::PrimAttribute& out, std::string& error)
{
    if (input.get_type() != Variant::DICTIONARY)
    {
        error = "expected an attribute Dictionary";
        return false;
    }
    const Dictionary attribute = input;
    const Variant name = attribute.get("name", Variant());
    const Variant type_name = attribute.get("type_name", Variant());
    const Variant value = attribute.get("value", Variant());
    if (name.get_type() != Variant::STRING || type_name.get_type() != Variant::STRING ||
        value.get_type() != Variant::DICTIONARY)
    {
        error = "name/type_name must be Strings and value must be a Dictionary";
        return false;
    }
    out.name = utf8(name);
    out.type_name = utf8(type_name);
    if (!pxr::SdfPath::IsValidNamespacedIdentifier(out.name) || out.name == "xformOpOrder" ||
        out.name.rfind("xformOp:", 0) == 0)
    {
        error = "invalid or reserved attribute name; use transform for placement";
        return false;
    }
    const Dictionary components = value;
    const Variant is_array = components.get("is_array", Variant());
    if (is_array.get_type() != Variant::BOOL)
    {
        error = "value.is_array must be an explicit bool";
        return false;
    }
    auto& decoded = out.value;
    decoded.is_array = is_array;
    if (!read_lane(components, "doubles", decoded.doubles, error, read_double) ||
        !read_lane(components, "ints", decoded.ints, error,
                   [](const Variant& v, int64_t& result) {
                       if (v.get_type() != Variant::INT) return false;
                       result = v;
                       return true;
                   }) ||
        !read_lane(components, "uints", decoded.uints, error, read_uint) ||
        !read_lane(components, "bools", decoded.bools, error,
                   [](const Variant& v, bool& result) {
                       if (v.get_type() != Variant::BOOL) return false;
                       result = v;
                       return true;
                   }) ||
        !read_lane(components, "strings", decoded.strings, error, [](const Variant& v, std::string& result) {
            if (v.get_type() != Variant::STRING) return false;
            result = utf8(v);
            return true;
        }))
        return false;

    const auto scalar = pxr::SdfSchema::GetInstance().FindType(pxr::TfToken(out.type_name));
    if (!scalar || scalar.IsArray())
    {
        error = "type_name must identify a known scalar USD type; use is_array for arrays";
        return false;
    }
    // Reuse the supported type/count/range rules without inspecting or mutating a stage.
    pxr::VtValue checked;
    const auto status =
        stage_ops::AttributeCodec::DecodeValue(decoded.is_array ? scalar.GetArrayType() : scalar, decoded, checked);
    if (status != stage_ops::DecodeStatus::Ok)
    {
        error = status == stage_ops::DecodeStatus::UnsupportedType ? "unsupported attribute type"
                                                                   : "invalid component lane, count or numeric range";
        return false;
    }
    return true;
}
} // namespace

bool decode_attributes(const godot::Variant& input, std::vector<net::model::PrimAttribute>& out, std::string& error)
{
    out.clear();
    error.clear();
    if (input.get_type() != Variant::ARRAY)
    {
        error = "attributes must be an Array";
        return false;
    }
    const Array attributes = input;
    std::vector<net::model::PrimAttribute> decoded;
    decoded.reserve(attributes.size());
    std::set<std::string> names;
    for (int64_t i = 0; i < attributes.size(); ++i)
    {
        net::model::PrimAttribute attribute;
        if (!read_attribute(attributes[i], attribute, error))
        {
            error = "attributes[" + std::to_string(i) + "]: " + error;
            return false;
        }
        if (!names.insert(attribute.name).second)
        {
            error = "attributes[" + std::to_string(i) + "]: duplicate attribute name";
            return false;
        }
        decoded.push_back(std::move(attribute));
    }
    out = std::move(decoded);
    return true;
}
} // namespace idtxflow::collab::attributes
