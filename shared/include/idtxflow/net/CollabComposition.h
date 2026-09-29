#pragma once

/**
 * @file CollabComposition.h
 * @brief Shared composition helpers that assemble the engine-agnostic adapters.
 *
 * A composition root (one per engine binding) is responsible for wiring the
 * CollabEngine's ports. Most of those adapters are engine-agnostic — the HTTP /
 * WebSocket transports, the process-wide token provider, and the wall clock — and
 * are identical for every binding. These helpers build that agnostic subset from
 * an injected ITransportFactory so each binding only has to supply the genuinely
 * engine-specific ports (main-thread dispatcher, frame ticker, stage bridge) and
 * pick a transport factory.
 *
 * The same helpers also build the JwtHttpFetcher used by the USD HTTP asset
 * resolver, which needs the same transport + shared token but is a separate
 * consumer from the engine.
 *
 * Ownership: transports are heap-owned and returned inside AgnosticTransports,
 * which the caller must keep alive for as long as the engine uses the ports.
 * The token provider and clock are process-wide singletons (borrowed, not owned).
 *
 * Separation of concerns: these helpers are the library-neutral mechanism (how
 * the agnostic adapters are assembled) and depend only on the ITransportFactory
 * port — never on a concrete transport library. Which library is used is the
 * policy, decided by the concrete ITransportFactory (e.g. IxTransportFactory)
 * that the composition root injects. Keep any transport library out of this file so
 * it stays reusable by every engine binding and injectable with fakes in tests.
 */

#include <memory>

#include <idtxflow/net/CollabEngine.h>
#include <idtxflow/net/adapters/auth/JwtHttpFetcher.h>
#include <idtxflow/net/adapters/auth/StaticTokenProvider.h>
#include <idtxflow/net/adapters/clock/SystemClock.h>
#include <idtxflow/net/ports/IHttpTransport.h>
#include <idtxflow/net/ports/ITransportFactory.h>
#include <idtxflow/net/ports/IWebSocketTransport.h>

namespace idtxflow
{
namespace net
{
    /// Owns the engine-agnostic transports produced for one engine instance.
    /// Keep this alive for as long as the engine holds the ports built from it.
    /// Only the shared HTTP transport is owned here; per-session WebSocket
    /// transports are minted by the engine on demand via the injected factory.
    struct AgnosticTransports
    {
        std::unique_ptr<ports::IHttpTransport> http;
    };

    /// Build the engine-agnostic HTTP transport from `factory` and fill the
    /// agnostic fields of `ports` (http, ws_factory, token, clock). The WebSocket
    /// is not built here: the engine mints one per session through `ws_factory`.
    /// The binding still supplies the engine-specific ports (dispatcher, ticker)
    /// itself. Returns the owned transports; the caller must keep the returned
    /// value — and `factory` — alive for as long as the engine uses the ports.
    inline AgnosticTransports make_agnostic_ports(ports::ITransportFactory& factory,
                                                  CollabPorts& ports)
    {
        AgnosticTransports transports;
        transports.http = factory.make_http();

        ports.http       = transports.http.get();
        ports.ws_factory = &factory;
        ports.token      = &adapters::StaticTokenProvider::instance();
        ports.clock      = &adapters::SystemClock::instance();
        return transports;
    }

    /// Build the JWT-injecting fetcher for the USD HTTP asset resolver: a fresh
    /// transport from `factory` plus the shared process-wide token (read at fetch
    /// time). Engine-agnostic; the concrete transport library is named only inside
    /// the factory.
    inline adapters::JwtHttpFetcher make_jwt_fetcher(ports::ITransportFactory& factory)
    {
        return adapters::JwtHttpFetcher{factory.make_http()};
    }

} // namespace net
} // namespace idtxflow
