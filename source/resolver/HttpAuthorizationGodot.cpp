#include "HttpAuthorizationGodot.h"

#include <godot_cpp/core/class_db.hpp>

#include <idtxflow/resolver/HttpResolver.h>

namespace godot
{

void IDTXFlowHttpAuthorization::_bind_methods()
{
    ClassDB::bind_static_method(
        "IDTXFlowHttpAuthorization",
        D_METHOD("set_bearer_token", "target_host", "token"),
        &IDTXFlowHttpAuthorization::set_bearer_token);
    ClassDB::bind_static_method(
        "IDTXFlowHttpAuthorization",
        D_METHOD("clear_authorization", "target_host"),
        &IDTXFlowHttpAuthorization::clear_authorization);
    ClassDB::bind_static_method(
        "IDTXFlowHttpAuthorization",
        D_METHOD("has_authorization", "target_host"),
        &IDTXFlowHttpAuthorization::has_authorization);
    ClassDB::bind_static_method(
        "IDTXFlowHttpAuthorization",
        D_METHOD("clear_all_authorizations"),
        &IDTXFlowHttpAuthorization::clear_all_authorizations);
}

bool IDTXFlowHttpAuthorization::set_bearer_token(const String& target_host, const String& token)
{
    return pxr::UsdHttpAssetResolver::SetBearerTokenForHost(
        target_host.utf8().get_data(),
        token.utf8().get_data());
}

bool IDTXFlowHttpAuthorization::clear_authorization(const String& target_host)
{
    return pxr::UsdHttpAssetResolver::ClearAuthorizationForHost(
        target_host.utf8().get_data());
}

bool IDTXFlowHttpAuthorization::has_authorization(const String& target_host)
{
    return pxr::UsdHttpAssetResolver::HasAuthorizationForHost(
        target_host.utf8().get_data());
}

void IDTXFlowHttpAuthorization::clear_all_authorizations()
{
    pxr::UsdHttpAssetResolver::ClearAllAuthorizations();
}

} // namespace godot
