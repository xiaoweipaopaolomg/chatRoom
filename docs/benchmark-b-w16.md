# Benchmark report

Status: **FORMAL NOT MET**

## scenario-b

Status: **FORMAL NOT MET**

Evidence: `docs/benchmark-results/scenario-b-1792483793_1178962.json`

| Metric | Value |
|---|---:|
| Git commit | 4d705f28a1e0475ee8a1a008b45fd63218c2c5d6 |
| Working tree dirty | True |
| OS | Linux 5.15.0-191-generic |
| Logical CPUs | 12 |
| Server image | sha256:9f9287f556c8972f1ef08052e39e36c3957e96b0a6633c7252cdbc384cbe40fa |
| MySQL image | sha256:efb212ddf03576a6c0b02b8d364e6a46f21e628308a719db0b0450f8817c0517 |
| Persistence | mysql |
| Business workers | 16 |
| MySQL pool size | 16 |
| Connections | 500 |
| Rooms | 50 |
| Duration | 300 s |
| Target rate | 1000.00 msg/s |
| Attempted send rate | 993.36 msg/s |
| Attempted sends | 299999 |
| Broadcast deliveries | 2196061 |
| Expected broadcast deliveries | 2513080 |
| Errors | 48691 |
| Unmatched | 31702 |
| P50 | 35977 µs |
| P95 | 61083401 µs |
| P99 | 62065129 µs |
| Host RAM | 100909907968 bytes |
| Server mean CPU | 39.28% |
| Server peak RSS | 22827008 bytes |
| MySQL mean CPU | 45.07% |
| MySQL peak RSS | 610484224 bytes |
| Resource samples | `docs/benchmark-results/scenario-b-1792483793_1178962-resources.json` |
| DB rooms added | 50 |
| DB membership events added | 500 |
| DB messages added | 219736 |
| Persisted rate | 732.45 msg/s |
| DB messages match sent | no |
