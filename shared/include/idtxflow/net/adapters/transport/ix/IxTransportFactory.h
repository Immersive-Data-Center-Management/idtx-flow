#pragma once

/**
 * @file IxTransportFactory.h
 * @brief Concrete ITransportFactory backed by the IXWebSocket library.
 *
 * This is the only place that names the IX transport library: it constructs the
 * IX HTTP and WebSocket adapters behind the transport ports. A composition root
 * selects this factory and injects it through ITransportFactory, so no other
 * code depends on IX. To switch libraries, rewrite the make_* bodies here or add
 * a sibling factory implementing ITransportFactory and select that instead.
 */

#include <memory>

#include <idtxflow/net/ports/ITransportFactory.h>
#include <idtxflow/net/adapters/transport/ix/IxHttpTransport.h>
#include <idtxflow/net/adapters/transport/ix/IxWebSocketTransport.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
    struct IxTransportFactory : ports::ITransportFactory
    {
        std::unique_ptr<ports::IHttpTransport> make_http() override
        {
            return std::make_unique<IxHttpTransport>();
        }

        std::unique_ptr<ports::IWebSocketTransport> make_websocket() override
        {
            return std::make_unique<IxWebSocketTransport>();
        }
    };

} // namespace adapters
} // namespace net
} // namespace idtxflow
