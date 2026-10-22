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

The server now retries disconnect cleanup when a worker lane is full, applies
backpressure to the bounded result queue, rejects joins whose member snapshot
would exceed the wire-frame limit, and drains submitted work during shutdown.

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

Build and launch the Qt 6 desktop client with:

```bash
cmake --preset linux-debug -DBUILD_QT_CLIENT=ON
cmake --build --preset linux-debug --target chatroom_qt_client
./out/build/linux-debug/src/qt_client/chatroom_qt_client
```

The client uses a Telegram-inspired two-column layout with room/member
navigation, colored avatars, incoming and outgoing message bubbles, live
connection state, and a responsive composer. Press Enter to send a message and
Shift+Enter to insert a new line.

## C++17 design

Every project target requests `cxx_std_17` through the shared CMake target. The
implementation uses C++17 features where they improve ownership, parsing, or
concurrency: `std::string_view` at protocol boundaries, `std::optional` and
`std::variant` for explicit outcomes, inline `constexpr` protocol limits,
structured bindings and if-initializers, `std::from_chars` for allocation-free
configuration parsing, `std::filesystem` for report publication,
`std::shared_mutex` for room reads, and `std::unique_ptr` for RAII-owned
implementation details.

`--smoke` runs two isolated 20-connection, 10-second scenarios and regenerates
`docs/benchmark.md` from their JSON evidence. Without `--smoke`, the script runs
the 1,000-connection/5,000 msg/s and 500-connection/1,000 msg/s, 300-second
scenarios. Each scenario gets its own temporary Compose project and database
volume; the script removes only those generated test resources. A smoke result
does not establish formal capacity. The report records when the working tree
contains uncommitted changes.

The Docker server defaults to 256 business workers, 128 MySQL connections,
and eight durable multi-row message writers. External MySQL deployments using
message batching must start MySQL with `innodb_autoinc_lock_mode=1`; the server
checks this so generated message IDs remain correctly mapped. See the
[MySQL auto-increment lock-mode reference](https://dev.mysql.com/doc/refman/8.4/en/innodb-auto-increment-handling.html).
Set `CHATROOM_WORKER_COUNT`, `CHATROOM_DB_POOL_SIZE`, and
`CHATROOM_BATCH_WRITER_COUNT` to override these limits for a deployment or an
isolated benchmark. `--diagnostic` runs a 30-second
500-connection/1,000 msg/s comparison; `--diagnostic-a` runs the
1,000-connection/5,000 msg/s case. `--formal-a` and `--formal-b` each run only
their corresponding 300-second case. The B acceptance goal also requires P99
below 100 ms. See [the tuning evidence](docs/benchmark-tuning.md) for results.
The five-minute A and B targets were met with **different business-worker
counts**: A used 256/128/8 workers/DB connections/batch writers; B used
128/128/8. Reproduce them separately with:

```bash
CHATROOM_WORKER_COUNT=256 CHATROOM_DB_POOL_SIZE=128 CHATROOM_BATCH_WRITER_COUNT=8 ./scripts/run-benchmark.sh --formal-a
CHATROOM_WORKER_COUNT=128 CHATROOM_DB_POOL_SIZE=128 CHATROOM_BATCH_WRITER_COUNT=8 ./scripts/run-benchmark.sh --formal-b
```

The unparameterized script uses the Docker default (256 workers) for both
scenarios; that single configuration has **not** met B's P99 target. The
[combined formal report](docs/benchmark.md) explicitly marks the two
profiles. See the [tuning history](docs/benchmark-tuning.md) for passing and
failed runs. Short diagnostics must not be treated as formal passes.
