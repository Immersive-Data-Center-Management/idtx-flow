# tests/

All tests for the IDTXFlow GDExtension client live here, grouped by kind.

## `unit/` — C++ unit suite (`scons test`)

Standalone console executable for the engine-agnostic core (`idtxflow::net`). It
links the core sources under test, the generated protobuf messages, and OpenUSD
(the REST codec parses JSON with `pxr::Js`); it links no godot-cpp and needs no
server, so the core stays testable without an engine or a live backend.

Run:

```bash
scons test
```

The `test` alias builds the executable and runs it, so a failing assertion fails
the SCons invocation. The build is delegated from the top-level `SConstruct` to
`tests/SConscript`; the default build is untouched.

Layout (grouped by the subsystem under test):

```
unit/
  support/   test framework, fakes, runner (test_framework.h, test_fakes.h, test_main.cpp) + HarnessTests
  net/       the collaboration-client core: Codec, Convention, RestCodec, RestClient, SessionSocket, Engine, IxHttpTransport(live)
  utils/     ThreadPool
```

Live transport tests (`IxHttpTransport`) compile only when `IDTX_TEST_BASE_URL`
is set in the environment; otherwise the suite stays IX- and server-free.

## `e2e/` — Python end-to-end harness

A headless "second client" that speaks the IDTX-Core `/api/v1` REST + WebSocket
protobuf contract, with no `protoc`/generated bindings. It automates the
backend/wire acceptance checks and provides `watch` / `send-xform` for the
editor-in-the-loop steps. See `e2e/README.md` for setup and usage.

```bash
python tests/e2e/idtx_e2e.py full
```
