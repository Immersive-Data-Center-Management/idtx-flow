#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{

/**
 * GDScript-facing API for configuring host-scoped HTTP resolver authorization.
 *
 * Example:
 * @code{.gdscript}
 * IDTXFlowHttpAuthorization.set_bearer_token(
 *     "https://assets.example.com",
 *     oauth_access_token)
 * @endcode
 */
class IDTXFlowHttpAuthorization : public Object
{
    GDCLASS(IDTXFlowHttpAuthorization, Object)

protected:
    static void _bind_methods();

public:
    static bool set_bearer_token(const String& target_host, const String& token);
    static bool clear_authorization(const String& target_host);
    static bool has_authorization(const String& target_host);
    static void clear_all_authorizations();
};

} // namespace godot
