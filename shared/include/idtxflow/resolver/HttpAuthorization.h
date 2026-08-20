#pragma once

/**
 * @file HttpAuthorization.h
 * @brief Thread-safe, host-scoped HTTP Authorization header storage.
 */

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace idtxflow::resolver
{

using HttpHeaders = std::map<std::string, std::string>;

/**
 * Process-wide authorization registry shared by the HTTP resolver and host integrations.
 *
 * Credentials are scoped to an HTTP origin (scheme, host, and non-default port). A target may
 * be supplied as a complete URL or as a bare host. Bare hosts intentionally default to HTTPS,
 * so a token registered for "assets.example.com" is never sent over plain HTTP.
 *
 * The registry stores authorization values only in memory. Callers can query whether an origin
 * is configured, but there is deliberately no public API that exposes credentials to scripts.
 */
class HttpAuthorizationRegistry final
{
public:
    static HttpAuthorizationRegistry& Instance()
    {
        static HttpAuthorizationRegistry registry;
        return registry;
    }

    /**
     * Set a Bearer token for a target origin.
     *
     * @param target A URL (only its origin is used) or an HTTPS host with an optional port.
     * @param token  OAuth access token without the "Bearer " prefix.
     * @return true when both values were valid and the token was stored.
     */
    bool SetBearerTokenForTarget(std::string_view target, std::string_view token)
    {
        if (!IsValidBearerToken(token))
        {
            return false;
        }

        std::string authorization = "Bearer ";
        authorization.append(token);
        return SetAuthorizationHeaderForTarget(target, authorization);
    }

    /**
     * Set a complete Authorization header value for a target origin.
     *
     * This lower-level API makes the registry extensible to authentication schemes other than
     * Bearer without allowing arbitrary HTTP headers to be injected.
     */
    bool SetAuthorizationHeaderForTarget(std::string_view target, std::string_view value)
    {
        auto origin = NormalizeOrigin(target);
        if (!origin || !IsValidHeaderValue(value))
        {
            return false;
        }

        std::unique_lock lock(mutex_);
        authorization_by_origin_[*origin] = std::string(value);
        return true;
    }

    /** Remove authorization configured for a target origin. */
    bool ClearAuthorizationForTarget(std::string_view target)
    {
        auto origin = NormalizeOrigin(target);
        if (!origin)
        {
            return false;
        }

        std::unique_lock lock(mutex_);
        return authorization_by_origin_.erase(*origin) != 0;
    }

    /** Remove all configured authorizations, for example when the user logs out. */
    void ClearAllAuthorizations()
    {
        std::unique_lock lock(mutex_);
        authorization_by_origin_.clear();
    }

    /** Check whether authorization is configured for a target origin. */
    bool HasAuthorizationForTarget(std::string_view target) const
    {
        auto origin = NormalizeOrigin(target);
        if (!origin)
        {
            return false;
        }

        std::shared_lock lock(mutex_);
        return authorization_by_origin_.contains(*origin);
    }

    /**
     * Return the request headers applicable to a URL.
     *
     * This method is intended for HTTP fetcher integrations. A copy is returned so token
     * replacement or logout can happen safely while a request is in flight.
     */
    HttpHeaders GetHeadersForUrl(std::string_view url) const
    {
        auto origin = NormalizeOrigin(url);
        if (!origin)
        {
            return {};
        }

        std::shared_lock lock(mutex_);
        auto authorization = authorization_by_origin_.find(*origin);
        if (authorization == authorization_by_origin_.end())
        {
            return {};
        }

        return {{"Authorization", authorization->second}};
    }

    /**
     * Convert a target URL or host to the canonical origin used as registry key.
     * Exposed primarily so other HTTP integrations can use identical scoping rules.
     */
    static std::optional<std::string> NormalizeOrigin(std::string_view target)
    {
        target = TrimAsciiWhitespace(target);
        if (target.empty() || ContainsControlCharacters(target))
        {
            return std::nullopt;
        }

        const std::size_t scheme_separator = target.find("://");
        std::string scheme;
        std::size_t authority_start = 0;
        if (scheme_separator == std::string_view::npos)
        {
            // A bare host is treated as HTTPS to avoid sending credentials over an accidental
            // downgrade to plain HTTP.
            scheme = "https";
        }
        else
        {
            scheme = ToLowerAscii(target.substr(0, scheme_separator));
            authority_start = scheme_separator + 3;
        }

        if (scheme != "http" && scheme != "https")
        {
            return std::nullopt;
        }

        const std::size_t authority_end = target.find_first_of("/?#", authority_start);
        std::string_view authority = target.substr(
            authority_start,
            authority_end == std::string_view::npos
                ? std::string_view::npos
                : authority_end - authority_start);

        if (authority.empty() || authority.find('@') != std::string_view::npos)
        {
            return std::nullopt;
        }

        std::string host;
        std::string_view port_text;
        if (authority.front() == '[')
        {
            const std::size_t closing_bracket = authority.find(']');
            if (closing_bracket == std::string_view::npos || closing_bracket == 1)
            {
                return std::nullopt;
            }

            host = ToLowerAscii(authority.substr(0, closing_bracket + 1));
            const std::string_view remainder = authority.substr(closing_bracket + 1);
            if (!remainder.empty())
            {
                if (remainder.front() != ':' || remainder.size() == 1)
                {
                    return std::nullopt;
                }
                port_text = remainder.substr(1);
            }
        }
        else
        {
            const std::size_t colon = authority.rfind(':');
            if (colon != std::string_view::npos)
            {
                // IPv6 literals must use brackets, otherwise the port is ambiguous.
                if (authority.find(':') != colon || colon == 0 || colon + 1 == authority.size())
                {
                    return std::nullopt;
                }
                port_text = authority.substr(colon + 1);
                authority = authority.substr(0, colon);
            }

            host = ToLowerAscii(authority);
            if (host.size() > 1 && host.back() == '.')
            {
                host.pop_back();
            }
        }

        if (!IsValidHost(host))
        {
            return std::nullopt;
        }

        std::optional<unsigned int> port;
        if (!port_text.empty())
        {
            unsigned int parsed_port = 0;
            for (char character : port_text)
            {
                if (character < '0' || character > '9')
                {
                    return std::nullopt;
                }
                parsed_port = parsed_port * 10 + static_cast<unsigned int>(character - '0');
                if (parsed_port > 65535)
                {
                    return std::nullopt;
                }
            }
            if (parsed_port == 0)
            {
                return std::nullopt;
            }
            port = parsed_port;
        }

        std::string origin = scheme + "://" + host;
        const bool is_default_port = port &&
            ((*port == 80 && scheme == "http") || (*port == 443 && scheme == "https"));
        if (port && !is_default_port)
        {
            origin += ':';
            origin += std::to_string(*port);
        }
        return origin;
    }

private:
    HttpAuthorizationRegistry() = default;

    static std::string_view TrimAsciiWhitespace(std::string_view value)
    {
        while (!value.empty() && IsAsciiWhitespace(value.front()))
        {
            value.remove_prefix(1);
        }
        while (!value.empty() && IsAsciiWhitespace(value.back()))
        {
            value.remove_suffix(1);
        }
        return value;
    }

    static bool IsAsciiWhitespace(char character)
    {
        switch (character)
        {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '\f':
        case '\v':
            return true;
        default:
            return false;
        }
    }

    static bool ContainsControlCharacters(std::string_view value)
    {
        return std::any_of(value.begin(), value.end(), [](char character)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            return byte < 0x20 || byte == 0x7f;
        });
    }

    static std::string ToLowerAscii(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return result;
    }

    static bool IsValidHost(std::string_view host)
    {
        if (host.empty())
        {
            return false;
        }

        if (host.front() == '[' && host.back() == ']')
        {
            return host.size() > 2 && std::all_of(host.begin() + 1, host.end() - 1, [](char character)
            {
                const unsigned char byte = static_cast<unsigned char>(character);
                return std::isxdigit(byte) || character == ':' || character == '.';
            });
        }

        return std::all_of(host.begin(), host.end(), [](char character)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            return std::isalnum(byte) || character == '.' || character == '-' || character == '_';
        });
    }

    static bool IsValidBearerToken(std::string_view token)
    {
        if (token.empty())
        {
            return false;
        }

        return std::none_of(token.begin(), token.end(), [](char character)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            return byte <= 0x20 || byte == 0x7f;
        });
    }

    static bool IsValidHeaderValue(std::string_view value)
    {
        if (value.empty())
        {
            return false;
        }

        return std::none_of(value.begin(), value.end(), [](char character)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            return byte < 0x20 || byte == 0x7f;
        });
    }

    mutable std::shared_mutex mutex_;
    std::map<std::string, std::string> authorization_by_origin_;
};

} // namespace idtxflow::resolver
