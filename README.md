# C++17 chat room service

This repository implements the accompanying service plan and Linux development
handbook:

- four-byte big-endian incremental TCP framing;
- strict JSON commands and stable response/event envelopes;
- bounded queues and a keyed FIFO executor;
- generation-safe sessions and thread-safe room membership;
- a persistence contract with a deterministic in-memory adapter;
- durable-first application operations and memory-first disconnect cleanup.
- Linux edge-triggered epoll/eventfd server with bounded output buffers;
- fixed-size MySQL Connector/C++ pool, migrations, and prepared statements;
- asynchronous Qt 6 Widgets client;
- deterministic three-client E2E scenario and single-process epoll load client.

## Build and test

Requirements: Linux, CMake 3.25+, Ninja, and a C++17 compiler. CMake first looks
for nlohmann/json 3.11.3 and GoogleTest 1.15.2 locally, then downloads those exact
versions when absent.

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

Sanitizer builds are deliberately separate:

```bash
cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan --output-on-failure

cmake --preset linux-tsan
cmake --build --preset linux-tsan
ctest --preset linux-tsan --output-on-failure
```

Run the server without MySQL using `config/server.example.json`, or use
`docker compose up -d --build --wait server` for the production adapter. See
[the protocol](docs/protocol.md), [architecture](docs/architecture.md), and
[demo checklist](docs/demo.md).

Optional targets are enabled with `-DBUILD_QT_CLIENT=ON` and
`-DBUILD_LOAD_TEST=ON`. Integration commands are:

```bash
./scripts/run-mysql-integration.sh
./scripts/run-e2e.sh
./scripts/run-benchmark.sh --smoke
```
