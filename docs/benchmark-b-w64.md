# Benchmark report

Status: **FORMAL MET**

## scenario-b

Status: **FORMAL MET**

Evidence: `docs/benchmark-results/scenario-b-1792484215_1184559.json`

| Metric | Value |
|---|---:|
| Git commit | 4d705f28a1e0475ee8a1a008b45fd63218c2c5d6 |
| Working tree dirty | True |
| OS | Linux 5.15.0-191-generic |
| Logical CPUs | 12 |
| Server image | sha256:1657d5d6538cbab69b769178bbd517b3582e5202a52267bcf7951db11887b87e |
| MySQL image | sha256:efb212ddf03576a6c0b02b8d364e6a46f21e628308a719db0b0450f8817c0517 |
| Persistence | mysql |
| Business workers | 64 |
| MySQL pool size | 64 |
| Connections | 500 |
| Rooms | 50 |
| Duration | 300 s |
| Target rate | 1000.00 msg/s |
| Attempted send rate | 999.91 msg/s |
| Attempted sends | 299998 |
| Broadcast deliveries | 2999980 |
| Expected broadcast deliveries | 2999980 |
| Errors | 0 |
| Unmatched | 0 |
| P50 | 37160 µs |
| P95 | 72718 µs |
| P99 | 382078 µs |
| Host RAM | 100909907968 bytes |
| Server mean CPU | 44.89% |
| Server peak RSS | 17166336 bytes |
| MySQL mean CPU | 53.84% |
| MySQL peak RSS | 791834624 bytes |
| Resource samples | `docs/benchmark-results/scenario-b-1792484215_1184559-resources.json` |
| DB rooms added | 50 |
| DB membership events added | 665 |
| DB messages added | 299998 |
| Persisted rate | 999.99 msg/s |
| DB messages match sent | yes |
