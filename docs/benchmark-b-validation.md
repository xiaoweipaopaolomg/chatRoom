# Benchmark report

Status: **FORMAL MET**

## scenario-b

Status: **FORMAL MET**

Evidence: `docs/benchmark-results/scenario-b-1792600882_1384802.json`

| Metric | Value |
|---|---:|
| Git commit | 4d705f28a1e0475ee8a1a008b45fd63218c2c5d6 |
| Working tree dirty | True |
| OS | Linux 5.15.0-191-generic |
| Logical CPUs | 12 |
| Compiler | 11.4.0 |
| Build type | Release |
| Server image | sha256:24a7b9f961b1e37a53e93ff01cd8fd32b91084e2ee19be1b3c8f34c172435150 |
| MySQL image | sha256:efb212ddf03576a6c0b02b8d364e6a46f21e628308a719db0b0450f8817c0517 |
| Persistence | mysql |
| Business workers | 128 |
| MySQL pool size | 128 |
| Batch writers | 8 |
| Connections | 500 |
| Rooms | 50 |
| Duration | 300 s |
| Connections initialized | 500 / 500 (100%) |
| Target rate | 1000.00 msg/s |
| Attempted send rate | 999.87 msg/s |
| Inbound throughput | 1000.00 msg/s |
| Attempted sends | 299999 |
| Broadcast deliveries | 2999990 |
| Outbound throughput | 9999.97 deliveries/s |
| Expected broadcast deliveries | 2999990 |
| Errors | 0 |
| Error rate | 0.0000% |
| Unmatched | 0 |
| P50 | 29806 µs |
| P95 | 47216 µs |
| P99 | 60280 µs |
| Persistence P99 target | < 100000 µs |
| Host RAM | 100909907968 bytes |
| Server mean CPU | 32.07% |
| Server peak RSS | 21512192 bytes |
| MySQL mean CPU | 32.54% |
| MySQL peak RSS | 879652864 bytes |
| Load-client mean CPU | 15.55% |
| Load-client peak RSS | 6610944 bytes |
| Resource samples | `docs/benchmark-results/scenario-b-1792600882_1384802-resources.json` |
| DB rooms added | 50 |
| DB membership events added | 632 |
| DB messages added | 299999 |
| Persisted rate | 1000.00 msg/s |
| DB messages match sent | yes |

Load command: `out/build/linux-release/src/load_client/chatroom_load_client --host 127.0.0.1 --port 7777 --connections 500 --rooms 50 --rate 1000 --duration 300 --output docs/benchmark-results/scenario-b-1792600882_1384802.json`

Reproduction command: `CHATROOM_WORKER_COUNT=128 CHATROOM_DB_POOL_SIZE=128 CHATROOM_BATCH_WRITER_COUNT=8 ./scripts/run-benchmark.sh --formal-b`
