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
 *
 * Separation of concerns: this factory is the policy (which transport library),
 * while CollabComposition is the library-neutral *mechanism* (how the agnostic
 * adapters are assembled into ports). Keep them apart — CollabComposition depends
 * only on the ITransportFactory port, never on IX, so it and any engine binding
 * stay swappable/testable. The composition root (e.g. register_types.cpp) is what
 * chooses this concrete factory.
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
