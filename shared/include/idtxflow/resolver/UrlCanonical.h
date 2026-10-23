#pragma once

/**
 * @file UrlCanonical.h
 * @brief One spelling per HTTP(S) URL, for layer identifiers and cache keys.
 *
 * A reference can name the same file in several ways ("./a.usda",
 * "dir/../a.usda", "my a.usda", "my%20a.usda"). The resolver turns each of
 * them into the same string, so a file has one layer identifier and one cache
 * entry, and a URL the server sends can be matched against them.
 *
 * The result is also what goes on the wire: a raw space in an HTTP request
 * line ends the request target, so the server rejects the request.
 */

#include <string>
#include <vector>

namespace idtxflow
{
namespace resolver
{

/**
 * Canonical form of an absolute URL: the path percent-decoded, its ".", ".."
 * and empty segments removed, then percent-encoded again (upper-case hex) for
 * every byte except RFC 3986 unreserved characters and "/". Scheme, authority,
 * query and fragment are kept as they are. A string without "://" is returned
 * unchanged. Applying it twice gives the same result as applying it once.
 *
 * "%2F" decodes to a separator, as the idtx-core download endpoint decodes it.
 */
inline std::string CanonicalUrl(const std::string& url)
{
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos)
    {
        return url;
    }

    const std::size_t path_start = url.find_first_of("/?#", scheme_end + 3);
    if (path_start == std::string::npos || url[path_start] != '/')
    {
        return url;
    }

    std::size_t path_end = url.find_first_of("?#", path_start);
    if (path_end == std::string::npos)
    {
        path_end = url.size();
    }

    const auto hex_value = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string decoded;
    decoded.reserve(path_end - path_start);
    for (std::size_t i = path_start; i < path_end; ++i)
    {
        if (url[i] == '%' && i + 2 < path_end)
        {
            const int high = hex_value(url[i + 1]);
            const int low = hex_value(url[i + 2]);
            if (high >= 0 && low >= 0)
            {
                decoded.push_back(static_cast<char>(high * 16 + low));
                i += 2;
                continue;
            }
        }
        decoded.push_back(url[i]);
    }

    std::vector<std::string> segments;
    bool trailing_slash = false;
    std::size_t pos = 0;
    while (pos <= decoded.size())
    {
        std::size_t next = decoded.find('/', pos);
        if (next == std::string::npos)
        {
            next = decoded.size();
        }
        const std::string segment = decoded.substr(pos, next - pos);
        const bool last = next == decoded.size();

        if (segment == "..")
        {
            if (!segments.empty()) segments.pop_back();
        }
        else if (!segment.empty() && segment != ".")
        {
            segments.push_back(segment);
        }
        if (last)
        {
            // "dir/", "dir/." and "dir/x/.." all name the directory.
            trailing_slash = segment.empty() || segment == "." || segment == "..";
        }
        pos = next + 1;
    }

    const auto unreserved = [](unsigned char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'
            || c == '_' || c == '~';
    };
    static const char* hex_digits = "0123456789ABCDEF";

    std::string out = url.substr(0, path_start);
    for (const std::string& segment: segments)
    {
        out.push_back('/');
        for (const unsigned char c: segment)
        {
            if (unreserved(c))
            {
                out.push_back(static_cast<char>(c));
            }
            else
            {
                out.push_back('%');
                out.push_back(hex_digits[c >> 4]);
                out.push_back(hex_digits[c & 0xF]);
            }
        }
    }
    if (segments.empty() || trailing_slash)
    {
        out.push_back('/');
    }
    out += url.substr(path_end);
    return out;
}

} // namespace resolver
} // namespace idtxflow
