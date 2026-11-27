#include <idtxflow/stage_ops/AttributeCodec.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <pxr/base/gf/half.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2h.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4h.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/tf/type.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usd/relationship.h>
#include <idtxflow/resolver/UsdAssetPath.h>
#include <set>

namespace idtxflow
{
namespace stage_ops
{

namespace
{

// Which repeated lane of AttributeValue a USD type uses.
enum class Lane
{
    Doubles,
    Ints,
    Uints,
    Bools,
    Strings,
};

// True when every lane except @p lane is empty. The used lane itself may be
// empty (an empty array authors no components), so this is a necessary but not
// sufficient check; component-count validation follows.
bool OnlyLane(const net::model::AttributeValue& in, const Lane lane)
{
    const bool d = static_cast<int>(in.doubles.size()) != 0;
    const bool i = static_cast<int>(in.ints.size()) != 0;
    const bool u = static_cast<int>(in.uints.size()) != 0;
    const bool b = static_cast<int>(in.bools.size()) != 0;
    const bool s = static_cast<int>(in.strings.size()) != 0;
    switch (lane)
    {
    case Lane::Doubles:
        return !i && !u && !b && !s;
    case Lane::Ints:
        return !d && !u && !b && !s;
    case Lane::Uints:
        return !d && !i && !b && !s;
    case Lane::Bools:
        return !d && !i && !u && !s;
    case Lane::Strings:
        return !d && !i && !u && !b;
    }
    return false;
}

// ---- Narrowing of a double component to the target scalar ------------------
// A finite input outside the target range is rejected (nullopt); NaN/+-inf are
// representable and pass through. See the numeric policy in the header.

std::optional<double> NarrowComponent(const double v, double*)
{
    return v;
}

std::optional<float> NarrowComponent(const double v, float*)
{
    if (std::isfinite(v) && (v > static_cast<double>(std::numeric_limits<float>::max()) ||
                             v < static_cast<double>(std::numeric_limits<float>::lowest())))
    {
        return std::nullopt;
    }
    return static_cast<float>(v);
}

std::optional<pxr::GfHalf> NarrowComponent(const double v, pxr::GfHalf*)
{
    // IEEE half has max magnitude 65504.
    constexpr double kHalfMax = 65504.0;
    if (std::isfinite(v) && (v > kHalfMax || v < -kHalfMax)) return std::nullopt;
    return pxr::GfHalf(static_cast<float>(v));
}

// ---- Integer/unsigned range checks -----------------------------------------

std::optional<int> ToInt32(const std::int64_t v)
{
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return std::nullopt;
    return static_cast<int>(v);
}

std::optional<std::uint8_t> ToUint8(const std::uint64_t v)
{
    if (v > std::numeric_limits<std::uint8_t>::max()) return std::nullopt;
    return static_cast<std::uint8_t>(v);
}

std::optional<unsigned int> ToUint32(const std::uint64_t v)
{
    if (v > std::numeric_limits<unsigned int>::max()) return std::nullopt;
    return static_cast<unsigned int>(v);
}

// ---- Generic lane decoders --------------------------------------------------
// Each reads WIDTH scalar components per logical element from one lane and
// builds a T via @p make (returning nullopt on a range/overflow failure). For a
// scalar exactly one element is required; for an array any whole number of
// elements (including zero) is accepted.

template <int Width, typename T, typename Make>
DecodeStatus DecodeDoubles(const net::model::AttributeValue& in, const bool is_array, Make make, pxr::VtValue& out)
{
    if (!OnlyLane(in, Lane::Doubles)) return DecodeStatus::InvalidValue;
    const int size = static_cast<int>(in.doubles.size());
    if (size % Width != 0) return DecodeStatus::InvalidValue;
    const int count = size / Width;

    double comps[Width];
    if (!is_array)
    {
        if (count != 1) return DecodeStatus::InvalidValue;
        for (int k = 0; k < Width; ++k)
            comps[k] = in.doubles[k];
        const std::optional<T> v = make(comps);
        if (!v) return DecodeStatus::InvalidValue;
        out = pxr::VtValue(*v);
        return DecodeStatus::Ok;
    }

    pxr::VtArray<T> arr(count);
    for (int e = 0; e < count; ++e)
    {
        for (int k = 0; k < Width; ++k)
            comps[k] = in.doubles[e * Width + k];
        const std::optional<T> v = make(comps);
        if (!v) return DecodeStatus::InvalidValue;
        arr[e] = *v;
    }
    out = pxr::VtValue(arr);
    return DecodeStatus::Ok;
}

template <int Width, typename T, typename Make>
DecodeStatus DecodeInts(const net::model::AttributeValue& in, const bool is_array, Make make, pxr::VtValue& out)
{
    if (!OnlyLane(in, Lane::Ints)) return DecodeStatus::InvalidValue;
    const int size = static_cast<int>(in.ints.size());
    if (size % Width != 0) return DecodeStatus::InvalidValue;
    const int count = size / Width;

    std::int64_t comps[Width];
    if (!is_array)
    {
        if (count != 1) return DecodeStatus::InvalidValue;
        for (int k = 0; k < Width; ++k)
            comps[k] = in.ints[k];
        const std::optional<T> v = make(comps);
        if (!v) return DecodeStatus::InvalidValue;
        out = pxr::VtValue(*v);
        return DecodeStatus::Ok;
    }

    pxr::VtArray<T> arr(count);
    for (int e = 0; e < count; ++e)
    {
        for (int k = 0; k < Width; ++k)
            comps[k] = in.ints[e * Width + k];
        const std::optional<T> v = make(comps);
        if (!v) return DecodeStatus::InvalidValue;
        arr[e] = *v;
    }
    out = pxr::VtValue(arr);
    return DecodeStatus::Ok;
}

template <int Width, typename T, typename Make>
DecodeStatus DecodeUints(const net::model::AttributeValue& in, const bool is_array, Make make, pxr::VtValue& out)
{
    if (!OnlyLane(in, Lane::Uints)) return DecodeStatus::InvalidValue;
    const int size = static_cast<int>(in.uints.size());
    if (size % Width != 0) return DecodeStatus::InvalidValue;
    const int count = size / Width;

    std::uint64_t comps[Width];
    if (!is_array)
    {
        if (count != 1) return DecodeStatus::InvalidValue;
        for (int k = 0; k < Width; ++k)
            comps[k] = in.uints[k];
        const std::optional<T> v = make(comps);
        if (!v) return DecodeStatus::InvalidValue;
        out = pxr::VtValue(*v);
        return DecodeStatus::Ok;
    }

    pxr::VtArray<T> arr(count);
    for (int e = 0; e < count; ++e)
    {
        for (int k = 0; k < Width; ++k)
            comps[k] = in.uints[e * Width + k];
        const std::optional<T> v = make(comps);
        if (!v) return DecodeStatus::InvalidValue;
        arr[e] = *v;
    }
    out = pxr::VtValue(arr);
    return DecodeStatus::Ok;
}

DecodeStatus DecodeBools(const net::model::AttributeValue& in, const bool is_array, pxr::VtValue& out)
{
    if (!OnlyLane(in, Lane::Bools)) return DecodeStatus::InvalidValue;
    const int count = static_cast<int>(in.bools.size());
    if (!is_array)
    {
        if (count != 1) return DecodeStatus::InvalidValue;
        out = pxr::VtValue(static_cast<bool>(in.bools[0]));
        return DecodeStatus::Ok;
    }
    pxr::VtArray<bool> arr(count);
    for (int e = 0; e < count; ++e)
        arr[e] = in.bools[e];
    out = pxr::VtValue(arr);
    return DecodeStatus::Ok;
}

template <typename T, typename Make>
DecodeStatus DecodeStrings(const net::model::AttributeValue& in, const bool is_array, Make make, pxr::VtValue& out)
{
    if (!OnlyLane(in, Lane::Strings)) return DecodeStatus::InvalidValue;
    const int count = static_cast<int>(in.strings.size());
    if (!is_array)
    {
        if (count != 1) return DecodeStatus::InvalidValue;
        out = pxr::VtValue(make(in.strings[0]));
        return DecodeStatus::Ok;
    }
    pxr::VtArray<T> arr(count);
    for (int e = 0; e < count; ++e)
        arr[e] = make(in.strings[e]);
    out = pxr::VtValue(arr);
    return DecodeStatus::Ok;
}

// ---- Generic lane encoders --------------------------------------------------

template <typename T, typename Put>
bool EncodeElems(const pxr::VtValue& in, const bool is_array, Put put, net::model::AttributeValue& out)
{
    if (!is_array)
    {
        if (!in.IsHolding<T>()) return false;
        put(in.UncheckedGet<T>(), out);
        return true;
    }
    if (!in.IsHolding<pxr::VtArray<T>>()) return false;
    for (const T& e: in.UncheckedGet<pxr::VtArray<T>>())
        put(e, out);
    return true;
}

} // namespace

DecodeStatus AttributeCodec::DecodeValue(const pxr::SdfValueTypeName& effective_type,
                                         const net::model::AttributeValue& in, pxr::VtValue& out)
{
    const pxr::SdfValueTypeName scalar = effective_type.GetScalarType();
    if (!scalar) return DecodeStatus::UnsupportedType;
    const bool is_array = effective_type.IsArray();

    // The caller derives the effective type's array-ness from in.is_array;
    // enforce that invariant defensively.
    if (is_array != in.is_array) return DecodeStatus::InvalidValue;

    const pxr::TfType t = scalar.GetType();

    // ---- bool ----
    if (t == pxr::TfType::Find<bool>()) return DecodeBools(in, is_array, out);

    // ---- unsigned (uints lane) ----
    if (t == pxr::TfType::Find<std::uint8_t>())
        return DecodeUints<1, std::uint8_t>(in, is_array, [](const std::uint64_t* c) { return ToUint8(c[0]); }, out);
    if (t == pxr::TfType::Find<unsigned int>())
        return DecodeUints<1, unsigned int>(in, is_array, [](const std::uint64_t* c) { return ToUint32(c[0]); }, out);
    if (t == pxr::TfType::Find<std::uint64_t>())
        return DecodeUints<1, std::uint64_t>(
            in, is_array, [](const std::uint64_t* c) -> std::optional<std::uint64_t> { return c[0]; }, out);

    // ---- signed integer (ints lane) ----
    if (t == pxr::TfType::Find<int>())
        return DecodeInts<1, int>(in, is_array, [](const std::int64_t* c) { return ToInt32(c[0]); }, out);
    if (t == pxr::TfType::Find<std::int64_t>())
        return DecodeInts<1, std::int64_t>(
            in, is_array, [](const std::int64_t* c) -> std::optional<std::int64_t> { return c[0]; }, out);
    if (t == pxr::TfType::Find<pxr::GfVec2i>())
        return DecodeInts<2, pxr::GfVec2i>(
            in, is_array,
            [](const std::int64_t* c) -> std::optional<pxr::GfVec2i> {
                const auto a = ToInt32(c[0]);
                const auto b = ToInt32(c[1]);
                if (!a || !b) return std::nullopt;
                return pxr::GfVec2i(*a, *b);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec3i>())
        return DecodeInts<3, pxr::GfVec3i>(
            in, is_array,
            [](const std::int64_t* c) -> std::optional<pxr::GfVec3i> {
                const auto a = ToInt32(c[0]);
                const auto b = ToInt32(c[1]);
                const auto d = ToInt32(c[2]);
                if (!a || !b || !d) return std::nullopt;
                return pxr::GfVec3i(*a, *b, *d);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4i>())
        return DecodeInts<4, pxr::GfVec4i>(
            in, is_array,
            [](const std::int64_t* c) -> std::optional<pxr::GfVec4i> {
                const auto a = ToInt32(c[0]);
                const auto b = ToInt32(c[1]);
                const auto d = ToInt32(c[2]);
                const auto e = ToInt32(c[3]);
                if (!a || !b || !d || !e) return std::nullopt;
                return pxr::GfVec4i(*a, *b, *d, *e);
            },
            out);

    // ---- floating scalars (doubles lane) ----
    if (t == pxr::TfType::Find<double>())
        return DecodeDoubles<1, double>(
            in, is_array, [](const double* c) { return NarrowComponent(c[0], static_cast<double*>(nullptr)); }, out);
    if (t == pxr::TfType::Find<float>())
        return DecodeDoubles<1, float>(
            in, is_array, [](const double* c) { return NarrowComponent(c[0], static_cast<float*>(nullptr)); }, out);
    if (t == pxr::TfType::Find<pxr::GfHalf>())
        return DecodeDoubles<1, pxr::GfHalf>(
            in, is_array, [](const double* c) { return NarrowComponent(c[0], static_cast<pxr::GfHalf*>(nullptr)); },
            out);

    // ---- double vectors ----
    if (t == pxr::TfType::Find<pxr::GfVec2d>())
        return DecodeDoubles<2, pxr::GfVec2d>(
            in, is_array, [](const double* c) -> std::optional<pxr::GfVec2d> { return pxr::GfVec2d(c[0], c[1]); }, out);
    if (t == pxr::TfType::Find<pxr::GfVec3d>())
        return DecodeDoubles<3, pxr::GfVec3d>(
            in, is_array, [](const double* c) -> std::optional<pxr::GfVec3d> { return pxr::GfVec3d(c[0], c[1], c[2]); },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4d>())
        return DecodeDoubles<4, pxr::GfVec4d>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfVec4d> { return pxr::GfVec4d(c[0], c[1], c[2], c[3]); }, out);

    // ---- float vectors (narrowed) ----
    if (t == pxr::TfType::Find<pxr::GfVec2f>())
        return DecodeDoubles<2, pxr::GfVec2f>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfVec2f> {
                const auto a = NarrowComponent(c[0], static_cast<float*>(nullptr));
                const auto b = NarrowComponent(c[1], static_cast<float*>(nullptr));
                if (!a || !b) return std::nullopt;
                return pxr::GfVec2f(*a, *b);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec3f>())
        return DecodeDoubles<3, pxr::GfVec3f>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfVec3f> {
                const auto a = NarrowComponent(c[0], static_cast<float*>(nullptr));
                const auto b = NarrowComponent(c[1], static_cast<float*>(nullptr));
                const auto d = NarrowComponent(c[2], static_cast<float*>(nullptr));
                if (!a || !b || !d) return std::nullopt;
                return pxr::GfVec3f(*a, *b, *d);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4f>())
        return DecodeDoubles<4, pxr::GfVec4f>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfVec4f> {
                const auto a = NarrowComponent(c[0], static_cast<float*>(nullptr));
                const auto b = NarrowComponent(c[1], static_cast<float*>(nullptr));
                const auto d = NarrowComponent(c[2], static_cast<float*>(nullptr));
                const auto e = NarrowComponent(c[3], static_cast<float*>(nullptr));
                if (!a || !b || !d || !e) return std::nullopt;
                return pxr::GfVec4f(*a, *b, *d, *e);
            },
            out);

    // ---- half vectors (narrowed) ----
    if (t == pxr::TfType::Find<pxr::GfVec2h>())
        return DecodeDoubles<2, pxr::GfVec2h>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfVec2h> {
                const auto a = NarrowComponent(c[0], static_cast<pxr::GfHalf*>(nullptr));
                const auto b = NarrowComponent(c[1], static_cast<pxr::GfHalf*>(nullptr));
                if (!a || !b) return std::nullopt;
                return pxr::GfVec2h(*a, *b);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec3h>())
        return DecodeDoubles<3, pxr::GfVec3h>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfVec3h> {
                const auto a = NarrowComponent(c[0], static_cast<pxr::GfHalf*>(nullptr));
                const auto b = NarrowComponent(c[1], static_cast<pxr::GfHalf*>(nullptr));
                const auto d = NarrowComponent(c[2], static_cast<pxr::GfHalf*>(nullptr));
                if (!a || !b || !d) return std::nullopt;
                return pxr::GfVec3h(*a, *b, *d);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4h>())
        return DecodeDoubles<4, pxr::GfVec4h>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfVec4h> {
                const auto a = NarrowComponent(c[0], static_cast<pxr::GfHalf*>(nullptr));
                const auto b = NarrowComponent(c[1], static_cast<pxr::GfHalf*>(nullptr));
                const auto d = NarrowComponent(c[2], static_cast<pxr::GfHalf*>(nullptr));
                const auto e = NarrowComponent(c[3], static_cast<pxr::GfHalf*>(nullptr));
                if (!a || !b || !d || !e) return std::nullopt;
                return pxr::GfVec4h(*a, *b, *d, *e);
            },
            out);

    // ---- matrix4d (row-major, 16 doubles) ----
    if (t == pxr::TfType::Find<pxr::GfMatrix4d>())
        return DecodeDoubles<16, pxr::GfMatrix4d>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfMatrix4d> {
                return pxr::GfMatrix4d(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12],
                                       c[13], c[14], c[15]);
            },
            out);

    // ---- quaternions (real, i, j, k) ----
    if (t == pxr::TfType::Find<pxr::GfQuatd>())
        return DecodeDoubles<4, pxr::GfQuatd>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfQuatd> { return pxr::GfQuatd(c[0], c[1], c[2], c[3]); }, out);
    if (t == pxr::TfType::Find<pxr::GfQuatf>())
        return DecodeDoubles<4, pxr::GfQuatf>(
            in, is_array,
            [](const double* c) -> std::optional<pxr::GfQuatf> {
                const auto r = NarrowComponent(c[0], static_cast<float*>(nullptr));
                const auto i = NarrowComponent(c[1], static_cast<float*>(nullptr));
                const auto j = NarrowComponent(c[2], static_cast<float*>(nullptr));
                const auto k = NarrowComponent(c[3], static_cast<float*>(nullptr));
                if (!r || !i || !j || !k) return std::nullopt;
                return pxr::GfQuatf(*r, *i, *j, *k);
            },
            out);

    // ---- strings lane: string / token / asset ----
    if (t == pxr::TfType::Find<std::string>())
        return DecodeStrings<std::string>(in, is_array, [](const std::string& s) { return s; }, out);
    if (t == pxr::TfType::Find<pxr::TfToken>())
        return DecodeStrings<pxr::TfToken>(in, is_array, [](const std::string& s) { return pxr::TfToken(s); }, out);
    if (t == pxr::TfType::Find<pxr::SdfAssetPath>())
        return DecodeStrings<pxr::SdfAssetPath>(
            in, is_array, [](const std::string& s) { return pxr::SdfAssetPath(s); }, out);

    return DecodeStatus::UnsupportedType;
}

bool AttributeCodec::EncodeValue(const pxr::SdfValueTypeName& effective_type, const pxr::VtValue& in,
                                 net::model::AttributeValue& out)
{
    out = {};
    const pxr::SdfValueTypeName scalar = effective_type.GetScalarType();
    if (!scalar) return false;
    const bool is_array = effective_type.IsArray();
    out.is_array = is_array;

    const pxr::TfType t = scalar.GetType();

    // ---- bool ----
    if (t == pxr::TfType::Find<bool>())
    {
        if (!is_array)
        {
            if (!in.IsHolding<bool>()) return false;
            out.bools.push_back(in.UncheckedGet<bool>());
            return true;
        }
        if (!in.IsHolding<pxr::VtArray<bool>>()) return false;
        for (const bool e: in.UncheckedGet<pxr::VtArray<bool>>())
            out.bools.push_back(e);
        return true;
    }

    // ---- unsigned ----
    if (t == pxr::TfType::Find<std::uint8_t>())
        return EncodeElems<std::uint8_t>(
            in, is_array, [](const std::uint8_t& v, net::model::AttributeValue& o) { o.uints.push_back(v); }, out);
    if (t == pxr::TfType::Find<unsigned int>())
        return EncodeElems<unsigned int>(
            in, is_array, [](const unsigned int& v, net::model::AttributeValue& o) { o.uints.push_back(v); }, out);
    if (t == pxr::TfType::Find<std::uint64_t>())
        return EncodeElems<std::uint64_t>(
            in, is_array, [](const std::uint64_t& v, net::model::AttributeValue& o) { o.uints.push_back(v); }, out);

    // ---- signed integer ----
    if (t == pxr::TfType::Find<int>())
        return EncodeElems<int>(
            in, is_array, [](const int& v, net::model::AttributeValue& o) { o.ints.push_back(v); }, out);
    if (t == pxr::TfType::Find<std::int64_t>())
        return EncodeElems<std::int64_t>(
            in, is_array, [](const std::int64_t& v, net::model::AttributeValue& o) { o.ints.push_back(v); }, out);
    if (t == pxr::TfType::Find<pxr::GfVec2i>())
        return EncodeElems<pxr::GfVec2i>(
            in, is_array,
            [](const pxr::GfVec2i& v, net::model::AttributeValue& o) {
                o.ints.push_back(v[0]);
                o.ints.push_back(v[1]);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec3i>())
        return EncodeElems<pxr::GfVec3i>(
            in, is_array,
            [](const pxr::GfVec3i& v, net::model::AttributeValue& o) {
                o.ints.push_back(v[0]);
                o.ints.push_back(v[1]);
                o.ints.push_back(v[2]);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4i>())
        return EncodeElems<pxr::GfVec4i>(
            in, is_array,
            [](const pxr::GfVec4i& v, net::model::AttributeValue& o) {
                o.ints.push_back(v[0]);
                o.ints.push_back(v[1]);
                o.ints.push_back(v[2]);
                o.ints.push_back(v[3]);
            },
            out);

    // ---- floating scalars ----
    if (t == pxr::TfType::Find<double>())
        return EncodeElems<double>(
            in, is_array, [](const double& v, net::model::AttributeValue& o) { o.doubles.push_back(v); }, out);
    if (t == pxr::TfType::Find<float>())
        return EncodeElems<float>(
            in, is_array,
            [](const float& v, net::model::AttributeValue& o) { o.doubles.push_back(static_cast<double>(v)); }, out);
    if (t == pxr::TfType::Find<pxr::GfHalf>())
        return EncodeElems<pxr::GfHalf>(
            in, is_array,
            [](const pxr::GfHalf& v, net::model::AttributeValue& o) {
                o.doubles.push_back(static_cast<double>(static_cast<float>(v)));
            },
            out);

    // ---- double vectors ----
    if (t == pxr::TfType::Find<pxr::GfVec2d>())
        return EncodeElems<pxr::GfVec2d>(
            in, is_array,
            [](const pxr::GfVec2d& v, net::model::AttributeValue& o) {
                o.doubles.push_back(v[0]);
                o.doubles.push_back(v[1]);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec3d>())
        return EncodeElems<pxr::GfVec3d>(
            in, is_array,
            [](const pxr::GfVec3d& v, net::model::AttributeValue& o) {
                o.doubles.push_back(v[0]);
                o.doubles.push_back(v[1]);
                o.doubles.push_back(v[2]);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4d>())
        return EncodeElems<pxr::GfVec4d>(
            in, is_array,
            [](const pxr::GfVec4d& v, net::model::AttributeValue& o) {
                o.doubles.push_back(v[0]);
                o.doubles.push_back(v[1]);
                o.doubles.push_back(v[2]);
                o.doubles.push_back(v[3]);
            },
            out);

    // ---- float vectors ----
    if (t == pxr::TfType::Find<pxr::GfVec2f>())
        return EncodeElems<pxr::GfVec2f>(
            in, is_array,
            [](const pxr::GfVec2f& v, net::model::AttributeValue& o) {
                o.doubles.push_back(v[0]);
                o.doubles.push_back(v[1]);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec3f>())
        return EncodeElems<pxr::GfVec3f>(
            in, is_array,
            [](const pxr::GfVec3f& v, net::model::AttributeValue& o) {
                o.doubles.push_back(v[0]);
                o.doubles.push_back(v[1]);
                o.doubles.push_back(v[2]);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4f>())
        return EncodeElems<pxr::GfVec4f>(
            in, is_array,
            [](const pxr::GfVec4f& v, net::model::AttributeValue& o) {
                o.doubles.push_back(v[0]);
                o.doubles.push_back(v[1]);
                o.doubles.push_back(v[2]);
                o.doubles.push_back(v[3]);
            },
            out);

    // ---- half vectors ----
    if (t == pxr::TfType::Find<pxr::GfVec2h>())
        return EncodeElems<pxr::GfVec2h>(
            in, is_array,
            [](const pxr::GfVec2h& v, net::model::AttributeValue& o) {
                o.doubles.push_back(static_cast<float>(v[0]));
                o.doubles.push_back(static_cast<float>(v[1]));
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec3h>())
        return EncodeElems<pxr::GfVec3h>(
            in, is_array,
            [](const pxr::GfVec3h& v, net::model::AttributeValue& o) {
                o.doubles.push_back(static_cast<float>(v[0]));
                o.doubles.push_back(static_cast<float>(v[1]));
                o.doubles.push_back(static_cast<float>(v[2]));
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfVec4h>())
        return EncodeElems<pxr::GfVec4h>(
            in, is_array,
            [](const pxr::GfVec4h& v, net::model::AttributeValue& o) {
                o.doubles.push_back(static_cast<float>(v[0]));
                o.doubles.push_back(static_cast<float>(v[1]));
                o.doubles.push_back(static_cast<float>(v[2]));
                o.doubles.push_back(static_cast<float>(v[3]));
            },
            out);

    // ---- matrix4d (row-major) ----
    if (t == pxr::TfType::Find<pxr::GfMatrix4d>())
        return EncodeElems<pxr::GfMatrix4d>(
            in, is_array,
            [](const pxr::GfMatrix4d& m, net::model::AttributeValue& o) {
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        o.doubles.push_back(m[i][j]);
            },
            out);

    // ---- quaternions (real, i, j, k) ----
    if (t == pxr::TfType::Find<pxr::GfQuatd>())
        return EncodeElems<pxr::GfQuatd>(
            in, is_array,
            [](const pxr::GfQuatd& q, net::model::AttributeValue& o) {
                o.doubles.push_back(q.GetReal());
                const pxr::GfVec3d im = q.GetImaginary();
                o.doubles.push_back(im[0]);
                o.doubles.push_back(im[1]);
                o.doubles.push_back(im[2]);
            },
            out);
    if (t == pxr::TfType::Find<pxr::GfQuatf>())
        return EncodeElems<pxr::GfQuatf>(
            in, is_array,
            [](const pxr::GfQuatf& q, net::model::AttributeValue& o) {
                o.doubles.push_back(static_cast<double>(q.GetReal()));
                const pxr::GfVec3f im = q.GetImaginary();
                o.doubles.push_back(im[0]);
                o.doubles.push_back(im[1]);
                o.doubles.push_back(im[2]);
            },
            out);

    // ---- strings lane ----
    if (t == pxr::TfType::Find<std::string>())
        return EncodeElems<std::string>(
            in, is_array, [](const std::string& s, net::model::AttributeValue& o) { o.strings.push_back(s); }, out);
    if (t == pxr::TfType::Find<pxr::TfToken>())
        return EncodeElems<pxr::TfToken>(
            in, is_array,
            [](const pxr::TfToken& s, net::model::AttributeValue& o) { o.strings.push_back(s.GetString()); }, out);
    if (t == pxr::TfType::Find<pxr::SdfAssetPath>())
        return EncodeElems<pxr::SdfAssetPath>(
            in, is_array,
            [](const pxr::SdfAssetPath& a, net::model::AttributeValue& o) { o.strings.push_back(a.GetAssetPath()); },
            out);

    return false;
}

bool prepare_attributes(const pxr::UsdStageRefPtr& stage, const pxr::SdfLayerHandle& target, const pxr::SdfPath& path,
                        const std::vector<net::model::PrimAttribute>& attributes, std::vector<PreparedAttribute>& out,
                        std::string& error, const std::string& type_hint)
{
    out.clear();
    error.clear();
    if (!stage || !target || !path.IsAbsolutePath() || !path.IsPrimPath() || path.ContainsPrimVariantSelection())
    {
        error = "invalid stage, edit layer or prim path";
        return false;
    }
    std::vector<PreparedAttribute> prepared;
    std::set<std::string> names;
    const auto prim = stage->GetPrimAtPath(path);
    pxr::TfToken schema_type(type_hint);
    // Inactive ancestors prune UsdPrim descendants, but their local typed definitions remain in Sdf.
    if (!prim && schema_type.IsEmpty())
        for (const auto& layer: stage->GetLayerStack())
            if (const auto spec = layer->GetPrimAtPath(path); spec && !spec->GetTypeName().IsEmpty())
            {
                schema_type = spec->GetTypeName();
                break;
            }
    const auto* definition = prim ? &prim.GetPrimDefinition()
                                  : pxr::UsdSchemaRegistry::GetInstance().FindConcretePrimDefinition(schema_type);
    for (const auto& attribute: attributes)
    {
        if (!pxr::SdfPath::IsValidNamespacedIdentifier(attribute.name) || !names.insert(attribute.name).second ||
            attribute.name == "xformOpOrder" || attribute.name.rfind("xformOp:", 0) == 0)
        {
            error = "invalid, duplicate or reserved attribute name";
            return false;
        }
        auto scalar = pxr::SdfSchema::GetInstance().FindType(pxr::TfToken(attribute.type_name));
        if (!scalar || scalar.IsArray())
        {
            error = "unknown or nonscalar type token";
            return false;
        }
        PreparedAttribute item;
        item.name = attribute.name;
        item.type = attribute.value.is_array ? scalar.GetArrayType() : scalar;
        if (AttributeCodec::DecodeValue(item.type, attribute.value, item.value) != DecodeStatus::Ok)
        {
            error = "unsupported type or invalid value components";
            return false;
        }
        const pxr::TfToken name(attribute.name);
        const auto property_path = path.AppendProperty(name);
        const auto existing = prim ? prim.GetAttribute(name) : pxr::UsdAttribute();
        const auto spec = target->GetAttributeAtPath(property_path);
        if ((prim && prim.GetRelationship(name)) || target->GetRelationshipAtPath(property_path))
        {
            error = "attribute name identifies a relationship";
            return false;
        }
        if ((existing && existing.GetTypeName() != item.type) || (spec && spec->GetTypeName() != item.type))
        {
            error = "attribute type mismatch";
            return false;
        }
        if (existing)
        {
            item.custom = existing.IsCustom();
            item.variability = existing.GetVariability();
        }
        else if (spec)
        {
            item.custom = spec->IsCustom();
            item.variability = spec->GetVariability();
        }
        else if (definition)
        {
            if (const auto declared = definition->GetAttributeDefinition(name))
            {
                if (declared.GetTypeName() != item.type)
                {
                    error = "schema attribute type mismatch";
                    return false;
                }
                item.custom = false;
                item.variability = declared.GetVariability();
            }
        }
        if (scalar == pxr::SdfValueTypeNames->Asset)
        {
            auto anchored = attribute.value;
            for (auto& identifier: anchored.strings)
            {
                if (identifier.empty()) continue;
                identifier =
                    resolver::AnchorAssetIdentifier(identifier, stage->GetRootLayer(), stage->GetPathResolverContext());
                if (identifier.empty())
                {
                    error = "asset value has no owning-layer anchor";
                    return false;
                }
            }
            if (AttributeCodec::DecodeValue(item.type, anchored, item.value) != DecodeStatus::Ok) return false;
        }
        prepared.push_back(std::move(item));
    }
    out = std::move(prepared);
    return true;
}

bool author_attributes(const pxr::SdfLayerHandle& layer, const pxr::SdfPath& path,
                       const std::vector<PreparedAttribute>& attributes)
{
    if (attributes.empty()) return true;
    if (!layer || !layer->PermissionToEdit()) return false;
    const auto prim = pxr::SdfCreatePrimInLayer(layer, path);
    if (!prim) return false;
    for (const auto& item: attributes)
    {
        auto spec = layer->GetAttributeAtPath(path.AppendProperty(pxr::TfToken(item.name)));
        if (!spec) spec = pxr::SdfAttributeSpec::New(prim, item.name, item.type, item.variability, item.custom);
        if (!spec || spec->GetTypeName() != item.type || !spec->SetDefaultValue(item.value)) return false;
    }
    return true;
}
} // namespace stage_ops
} // namespace idtxflow
