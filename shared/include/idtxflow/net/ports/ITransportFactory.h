#pragma once

/**
 * @file ITransportFactory.h
 * @brief Port for constructing the concrete transports the client needs.
 *
 * Callers depend only on this interface and the transport interfaces it returns;
 * they name no HTTP/WebSocket library. A composition root supplies one concrete
 * factory - the single place that names a specific transport library. Swapping
 * libraries (or injecting fakes in tests) is done by providing a different
 * ITransportFactory, with no change to the core or its consumers.
 */

#include <memory>

#include <idtxflow/net/ports/IHttpTransport.h>
#include <idtxflow/net/ports/IWebSocketTransport.h>

namespace idtxflow
{
namespace net
{
namespace ports
{
    struct ITransportFactory
    {
        virtual ~ITransportFactory() = default;

        /// Create an HTTP transport (fresh instance, ownership transferred).
        virtual std::unique_ptr<IHttpTransport> make_http() = 0;

        /// Create a WebSocket transport (fresh instance, ownership transferred).
        virtual std::unique_ptr<IWebSocketTransport> make_websocket() = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
