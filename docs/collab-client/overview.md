# IDTXFlow Collaboration Client

The collaboration client is the part of IDTXFlow that connects the Godot editor to
an IDTX backend: it authenticates, browses and downloads USD assets from an asset
server, and — for collaboration sessions — opens a live WebSocket and keeps prim
transforms in sync between collaborating peers in real time. Local (res://) imports
share the same import UI but need no backend.

This folder documents that client end to end. Start here, then follow the links
below into each layer.

**Architecture style.** The client is built as a **hexagonal (ports & adapters)**
system: a pure, standard-library-only **core** (`idtxflow::net`) that knows nothing
about Godot, the WebSocket library (IXWebSocket), protobuf-on-the-wire, or OpenUSD.
Everything engine- or library-specific lives in thin **adapters** behind the core's
**ports**. The engine-specific part is the whole **Godot binding** — `source/collab/*`
(`IdtxClient`, `StageBridge`, `TransformCodec`, `Dispatcher`, `Ticker`), the USD nodes
in `source/nodes/*`, and module registration (`register_types.cpp`). Within that
binding, `StageBridge` is the single place Godot and OpenUSD types meet. A different
host engine reuses the entire core and swaps only these adapters — see
[net-core.md](net-core.md) for the ports contract and
[Extending the system](net-core.md#extending-the-system) for how new behavior is added.

---

## The three layers

The client is built as a **hexagonal (ports & adapters)** system split across three
layers, so the networking/protocol logic is engine-agnostic and reusable, and only a
thin layer is specific to Godot.

```mermaid
graph TB
    subgraph UI["Editor UI — GDScript (addons/IDTXFlow/)"]
        PLUGIN["plugin.gd<br/>editor plugin + main screen"]
        WIZ["editor/import/<br/>import wizard: steps, providers, widgets"]
    end

    subgraph BIND["Godot binding — C++ GDExtension (source/collab/, source/nodes/)"]
        CLIENT["IdtxClient<br/>engine singleton · observer · composition root"]
        BRIDGE["StageBridge<br/>USD stage &lt;-&gt; Godot nodes"]
        NODES["USD nodes<br/>UsdStageNode3D, UsdXformNode3D, ..."]
    end

    subgraph CORE["Engine-agnostic net core — C++ (shared/idtxflow/net/)"]
        ENGINE["CollabEngine + CollabObserver<br/>state, gating, session flow"]
        PROTO["protocol/ RestClient + SessionSocket<br/>wire/ WireCodec · model/"]
        PORTS["ports/ (interfaces) + adapters/<br/>(IX transport, token, clock)"]
    end

    BACKEND["IDTX backend<br/>REST /api/v1 + /ws WebSocket"]

    UI -->|via IdtxClient engine singleton| BIND
    BIND -->|ports injected, observer reports back| CORE
    CORE -->|HTTP + WebSocket| BACKEND

    style CORE fill:#e3f2fd,stroke:#1565c0,color:#000
    style BIND fill:#e8f5e9,stroke:#2e7d32,color:#000
    style UI fill:#fff3e0,stroke:#e65100,color:#000
```

- **Engine-agnostic net core** — all networking, protocol, and domain logic. Names no
  Godot and no specific HTTP/WebSocket library; talks to the world only through *ports*
  and reports results through a single *observer*. Reusable by any host engine.
- **Godot binding** — the only Godot-specific C++. Implements the engine-specific ports
  (main-thread dispatch, per-frame tick, USD stage bridge), converts between Godot and
  core types, and is the composition root that wires everything together.
- **Editor UI** — the GDScript editor plugin and the import wizard the user drives.

---

## Architecture

### Engine-specific layer — Godot binding + editor UI

```mermaid
---
config:
  layout: elk
---
graph TB
    subgraph ENGINESPECIFIC["Engine-specific — Godot binding + editor UI (godot-cpp + USD)"]
        subgraph UI["Editor UI — GDScript (addons/IDTXFlow/)"]
            PLUGIN["plugin.gd<br/>EditorPlugin: main screen + inspector + settings"]
            WIZ["editor/import/import_manager.gd<br/>3-step wizard root"]
            WIZUI["wizard pieces<br/>steps · providers · widgets"]
            SESS["editor/session_scene/<br/>coordinator + live-session indicators"]
        end

        subgraph BIND["Godot binding — source/collab, source/nodes (godot-cpp + USD)"]
            CLIENT["IdtxClient<br/>Node · singleton · CollabObserver · composition root"]
            BRIDGE["StageBridge<br/>IStageBridge — only godot+USD meeting point"]
            CODEC2["TransformCodec<br/>Transform3D &lt;-&gt; PrimEdit/Mat4"]
            DISP["Dispatcher<br/>IMainThreadDispatcher"]
            TICK["Ticker<br/>IFrameTicker"]
            NODES["UsdStageNode3D + converted prim nodes"]
            REG["register_types.cpp<br/>module boot"]
        end
    end

    ENGINEREF["→ CollabEngine<br/>engine-agnostic core (see next graph)"]
    PORTSREF["→ ports/*<br/>engine-agnostic core contract (see next graph)"]

    PLUGIN --> WIZ
    WIZ --> WIZUI
    PLUGIN -->|owns + wires| SESS
    WIZ -.triggers create/join.- SESS

    WIZ -->|Engine.get_singleton · methods · signals · on_done| CLIENT
    WIZUI --> CLIENT
    REG -->|create singleton, initialize| CLIENT
    NODES -->|transform changed| CLIENT
    BRIDGE -.uses.- CODEC2
    CLIENT -.uses.- CODEC2

    CLIENT -->|owns and drives| ENGINEREF
    BRIDGE -->|implements IStageBridge| PORTSREF
    DISP -->|implements IMainThreadDispatcher| PORTSREF
    TICK -->|implements IFrameTicker| PORTSREF

    classDef engine fill:#fff0e6,stroke:#d9822b,color:#5c2d0b;
    classDef ref fill:#eeeeee,stroke:#888888,color:#333333,stroke-dasharray: 5 3;
    class ENGINESPECIFIC,UI,PLUGIN,WIZ,WIZUI,SESS,BIND,CLIENT,BRIDGE,CODEC2,DISP,TICK,NODES,REG engine;
    class ENGINEREF,PORTSREF ref;
```

**↕ Boundary: `IdtxClient` (engine-specific binding) owns and drives `CollabEngine` (engine-agnostic core), and the binding implements the core's ports. The two graphs meet at this seam. The binding's `StageBridge` is also the one place that authors and reads the live USD stage.**

### Engine-agnostic layer — idtxflow::net core + adapters

```mermaid
---
config:
  layout: elk
---
graph TB
    CLIENTREF["IdtxClient<br/>engine-specific binding (see previous graph)"]

    subgraph AGNOSTIC["Engine-agnostic — idtxflow::net + IX/std-library adapters (no Godot)"]
        subgraph CORE["Net core — idtxflow::net (engine- and transport-agnostic: standard library only)"]
            ENGINE["CollabEngine<br/>state · gating · session flow · poll()"]
            REST["RestClient + RestCodec (JSON)"]
            SOCK["SessionSocket<br/>coalescing · inbound dispatch"]
            CODEC["wire/WireCodec<br/>protobuf &lt;-&gt; model"]
            MODEL["model/* + ConventionMath<br/>plain data + matrix conventions"]
            subgraph PORTS["ports/* — interfaces the core requires"]
                OBS["CollabObserver<br/>results out"]
                PBRIDGE["IStageBridge"]
                PDISP["IMainThreadDispatcher"]
                PTICK["IFrameTicker"]
                PHTTP["IHttpTransport"]
                PWS["IWebSocketTransport"]
                PTOK["ITokenProvider"]
                PCLK["IClock"]
            end
        end

        subgraph ADAPT["Agnostic adapters — shared/.../net/adapters (IX + std library, no Godot)"]
            HTTP["IxHttpTransport<br/>IHttpTransport"]
            WS["IxWebSocketTransport<br/>IWebSocketTransport"]
            TFAC["IxTransportFactory<br/>ITransportFactory — creates IX transports"]
            TOK["StaticTokenProvider<br/>ITokenProvider"]
            FETCH["JwtHttpFetcher<br/>auth fetcher for USD HTTP assets"]
            CLK["SystemClock<br/>IClock"]
        end
    end

    BACKEND["IDTX-Core backend<br/>/api/v1 REST · /ws WebSocket (protobuf)"]
    USDSTAGE["Live USD stage<br/>+ converted Godot nodes"]
    BINDREF["Godot binding adapters<br/>StageBridge · Dispatcher · Ticker (see previous graph)"]

    CLIENTREF -->|owns and drives| ENGINE
    CLIENTREF -.implements.-> OBS

    ENGINE -->|reports results| OBS
    ENGINE --> REST
    ENGINE --> SOCK
    REST --> MODEL
    SOCK --> CODEC
    CODEC --> MODEL
    ENGINE --> PBRIDGE
    ENGINE --> PDISP
    ENGINE --> PTICK
    REST --> PHTTP
    REST --> PTOK
    SOCK --> PWS
    SOCK --> PCLK
    HTTP -->|implements| PHTTP
    WS -->|implements| PWS
    TOK -->|implements| PTOK
    CLK -->|implements| PCLK
    BINDREF -->|implements| PBRIDGE
    BINDREF -->|implements| PDISP
    BINDREF -->|implements| PTICK
    TFAC -.creates.-> HTTP
    TFAC -.creates.-> WS
    TOK -->|bearer token| FETCH

    HTTP -->|REST requests| BACKEND
    WS -->|session WebSocket| BACKEND
    FETCH -->|authenticated GET /download| BACKEND
    FETCH -.->|fetches server assets for| USDSTAGE

    classDef agnostic fill:#e6f2ff,stroke:#1f6feb,color:#0b2d5c;
    classDef ref fill:#eeeeee,stroke:#888888,color:#333333,stroke-dasharray: 5 3;
    classDef ext fill:#dddddd,stroke:#888888,color:#333333;
    class AGNOSTIC,CORE,ENGINE,REST,SOCK,CODEC,MODEL,PORTS,OBS,PBRIDGE,PDISP,PTICK,PHTTP,PWS,PTOK,PCLK agnostic;
    class ADAPT,HTTP,WS,TFAC,TOK,FETCH,CLK agnostic;
    class CLIENTREF,BINDREF ref;
    class BACKEND,USDSTAGE ext;
```

---



## Where things live

| Layer | Location | Key pieces |
|---|---|---|
| Net core | `shared/idtxflow/net/` | `CollabEngine`, `CollabObserver`, `protocol/` (RestClient, SessionSocket), `wire/WireCodec`, `model/`, `ports/`, `adapters/`, `CollabComposition` |
| Godot binding | `source/collab/`, `source/nodes/`, `source/register_types.cpp` | `IdtxClient`, `StageBridge`, `TransformCodec`, `Dispatcher`, `Ticker`, USD nodes, module boot |
| Editor UI | `addons/IDTXFlow/` | `plugin.gd`, `editor/import/` (wizard), `editor/session_scene/` (session-scene lifecycle + indicators), `editor/inspector/` |

---

## Documents

- **[net-core.md](net-core.md)** — the engine-agnostic net stack: layers, ports & adapters, threading,
  REST + session protocol, wire codec, model/convention math, composition, and what a new
  host engine reuses vs. implements.
- **[godot-binding.md](godot-binding.md)** — the Godot C++ side: `IdtxClient`, `StageBridge`, `TransformCodec`,
  dispatcher/ticker/clock, USD nodes, module registration, and the USD HTTP asset resolver.
- **[import-manager.md](import-manager.md)** — the editor UI: the plugin/main screen and the import wizard
  (steps, providers, widgets) for local and server imports — plus the editor session-scene coordinator,
  transient session scenes, and live-session indicators.
- **[flows.md](flows.md)** — end-to-end user flows (local import, server download, create/join collaboration
  session) and the session lifecycle.
- **[transform-sync-flow.md](transform-sync-flow.md)** — deep dive on how a single transform
  edit travels inbound and outbound between the editor and collaborating peers.

---

## Supported flows at a glance

- **Local import** — pick a USD file from `res://` and import it into the current or a new scene. No backend.
- **Server download import** — log in to an asset server, browse its files, and import a USD via an authenticated download (into the current or a new scene). No session.
- **Create collaboration session** — as above, but open a live session for the file (single-edit or collaborative-edit): a WebSocket is opened and the stage is wired for real-time transform sync with other peers.
- **Join collaboration session** — join a running collaborative-edit session for the selected file; same live transform sync.
