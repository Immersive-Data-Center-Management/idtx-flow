#pragma once

#include <string>

namespace idtxflow
{
namespace net
{
namespace adapters
{

/// Default IXWebSocket ``SocketTLSOptions::caFile`` value for the current
/// platform.
///
/// IXWebSocket only resolves "SYSTEM" against the OS trust store on Windows
/// (the MbedTLS backend guards the system-store code with ``#ifdef _WIN32``),
/// and an empty caFile makes MbedTLS fail to parse. On Android we therefore use
/// "NONE", which disables certificate verification.
///
/// NOTE (demo-only): "NONE" means the server certificate is NOT verified on
/// Android — acceptable for the stage/showroom build, but a real deployment
/// should ship a CA bundle and point caFile at it instead.
inline std::string default_ca_file()
{
#if defined(__ANDROID__)
    return "NONE";
#else
    return "SYSTEM";
#endif
}

} // namespace adapters
} // namespace net
} // namespace idtxflow
