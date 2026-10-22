# MySQL concurrency tuning (2026-10-20)

These are 30-second diagnostic runs on one Linux host, not the required
300-second capacity result. Each run used an isolated Compose project and fresh
MySQL volume. The client attempted the specified rate with ten members per
room. Database rows were counted before and immediately after the run; a
failed run can still have writes queued at the counting point. `SERVER_BUSY`
counts are server responses, while `unmatched` counts requests without a
self-echo after the two-second drain.

| Connections / rooms | Target msg/s | Workers / DB pool | Sent | `SERVER_BUSY` | Unmatched | DB message delta | P95 self-echo | Evidence |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 500 / 50 | 1,000 | 4 / 8 | 29,999 | 9,633 | 7,379 | 13,037 | 18.07 s | [JSON](benchmark-results/diagnostic-w4-p8-1792482987_1160726.json) |
| 500 / 50 | 1,000 | 16 / 16 | 29,999 | 0 | 0 | 29,999 | 33.56 ms | [JSON](benchmark-results/diagnostic-w16-p16-1792483081_1163937.json) |
| 1,000 / 100 | 5,000 | 16 / 16 | 149,999 | 82,160 | 30,470 | 37,573 | 23.28 s | [JSON](benchmark-results/diagnostic-a-w16-p16-1792483169_1165831.json) |
| 1,000 / 100 | 5,000 | 32 / 32 | 149,994 | 29,640 | 61,922 | 58,693 | 19.28 s | [JSON](benchmark-results/diagnostic-a-w32-p32-1792483479_1172170.json) |
| 1,000 / 100 | 5,000 | 64 / 64 | 149,998 | 0 | 69,016 | 81,432 | 15.08 s | [JSON](benchmark-results/diagnostic-a-w64-p64-1792483569_1174177.json) |
| 1,000 / 100 | 5,000 | 128 / 128 | 149,998 | 0 | 52,053 | 98,390 | 12.11 s | [JSON](benchmark-results/diagnostic-a-w128-p128-1792483664_1176336.json) |

The 30-second 16/16 success did **not** sustain for 300 seconds: its formal B
run persisted only 219,736 of 299,999 attempted messages, with 48,691
`SERVER_BUSY` responses and 31,702 unmatched requests
([report](benchmark-b-w16.md), [JSON](benchmark-results/scenario-b-1792483793_1178962.json)).
With 64/64, the same 300-second B case persisted all 299,998 sends, delivered
all 2,999,980 expected broadcasts, had zero errors and unmatched requests,
and measured 72.72 ms P95 self-echo latency
([first report](benchmark-b-w64.md), [JSON](benchmark-results/scenario-b-1792484215_1184559.json)).
At that stage, the Docker default was 64/64. This choice used more resources; the
64/64 formal run recorded 792 MB peak MySQL RSS and 17 MB peak server RSS.

At that stage, the 5,000 msg/s A requirement was **not met**: even 128/128 left a
large write backlog in the short diagnostic. The `SERVER_BUSY` count falling
to zero at 64/64 means queue acceptance must not be mistaken for completed
persistence. Further work should measure per-operation database time and
evaluate a durable batch-write design that preserves per-room message order
and broadcasts only committed messages.

The server now logs `server.mysql_message_metrics` at clean shutdown, with
attempt/failure counts and aggregate microseconds for the full append, INSERT
execution, and ID lookup. These phase totals were added after the runs above;
they are not retroactively available for those measurements.

One subsequent 64/64 A diagnostic with instrumentation again failed its
30-second window: 149,996 attempted sends, 68,160 unmatched at the two-second
drain, and 82,306 database rows added at the count point
([JSON](benchmark-results/diagnostic-a-w64-p64-1792510437_1214930.json)).
After the server's shutdown drain, its [log](benchmark-results/diagnostic-a-w64-p64-1792510437_1214930-server.log)
reported 150,295 append attempts including 299 warmup messages, with zero
repository failures. Mean time per append was 14.95 ms: 14.21 ms in INSERT
execution and 0.36 ms in the ID query. The remaining time includes pool
acquisition, statement preparation, and result construction. This points to
the INSERT/commit path as the dominant measured cost in that run; removing the
ID query alone would not close the throughput gap. These are means over the
entire execution, including work drained after the client measurement ended.

The instrumented build at that stage also passed a new 300-second 64/64 B run:
299,998 persisted sends, 2,999,980 matching broadcasts, zero errors or
unmatched requests, and 64.94 ms P95
([JSON](benchmark-results/scenario-b-1792510593_1217134.json),
[server log](benchmark-results/scenario-b-1792510593_1217134-server.log)). Its
server log reported 302,997 append attempts including 2,999 warmup messages, zero
failures, and mean append/INSERT/ID-query times of 28.94/28.50/0.21 ms.

Reproduce a short comparison with
`CHATROOM_WORKER_COUNT=32 CHATROOM_DB_POOL_SIZE=32 ./scripts/run-benchmark.sh --diagnostic-a`.
The script removes only its own temporary containers, network and volume.

## Durable batching and room-lock concurrency (2026-10-21)

The new optional MySQL message batch writer groups messages from different
rooms into one multi-row INSERT and transaction. It returns per-message IDs
only after COMMIT succeeds. The server checks `innodb_autoinc_lock_mode=1` at
startup when batching is enabled. The Docker MySQL service sets this mode;
the unbatched repository path remains available with `writer_count=0`.

The first design (many single-row INSERTs in one transaction) did not help:
the 64/64 diagnostic still had 78,296 unmatched messages
([JSON](benchmark-results/diagnostic-a-w64-p64-1792550099_1249530.json)).
Multi-row INSERT reduced this to 50,037, but still left a backlog
([JSON](benchmark-results/diagnostic-a-w64-p64-1792550307_1252864.json)).
Pre-encoding each chat event once and avoiding redundant epoll interest
updates had no measurable gain in the next 64/64 run
([JSON](benchmark-results/diagnostic-a-w64-p64-1792550482_1255538.json)).
At 128/128, the backlog fell to 29,315
([JSON](benchmark-results/diagnostic-a-w128-p128-1792550613_1257837.json)).

The room lock table had only 64 stripes for 100 rooms. Increasing it to 256,
while retaining one lock per room hash, removed those avoidable collisions.
With 128 workers and 128 MySQL connections, the next 30-second A diagnostic
persisted all 149,999 messages, delivered all 1,499,990 broadcasts, had zero
errors/unmatched requests, and measured 159.07 ms P95 and 257.91 ms P99
([JSON](benchmark-results/diagnostic-a-w128-p128-1792550715_1260272.json)).
This is a short diagnostic, not a five-minute capacity result.

The previous B report was incorrectly labeled MET by the report renderer:
its P99 was 116.29 ms, exceeding the handbook's strict 100 ms target. The
renderer now enforces this threshold for the formal B scenario. A fresh
five-minute B validation is required for the new batched configuration.

That validation has now passed on the 128/128 batched build: 299,999 sends
were persisted, all 2,999,990 expected broadcasts arrived, there were zero
errors and unmatched requests, and P99 was 54.67 ms, below the strict 100 ms
goal ([JSON](benchmark-results/scenario-b-1792551356_1268561.json)).

The first 300-second A validation of the 128/128, 256-stripe build did **not**
sustain the short-run result. It attempted 1,499,998 sends but recorded
177,949 `SERVER_BUSY` responses, 256,775 unmatched requests, and 1,066,416
message rows at the measurement point. Server mean CPU was 92.42% of one core
while MySQL mean CPU was 28.10% of one core. That run was **FORMAL NOT MET**
([raw JSON](benchmark-results/scenario-a-1792550851_1262780.json)). This is
evidence of a sustained service-side bottleneck, not a passing capacity claim.

Longer diagnostics sampled the load client as well as the two containers.
With 128 workers, 4 writers, batches of 32 and a 2 ms collection window,
the 120-second A run left 59,828 unmatched sends and had mean CPU of 90.84%
server, 32.15% MySQL, and 61.00% load client
([JSON](benchmark-results/diagnostic-a-w128-p128-d120-1792551762_1273669.json)).
Growing batches to 64 with a 5 ms window reduced aggregate commit time but
left 63,103 unmatched sends, so that tuning was discarded
([JSON](benchmark-results/diagnostic-a-w128-p128-d120-1792551997_1277294.json)).
Eight writers with 32/2 ms left 56,496 unmatched at 128 workers
([JSON](benchmark-results/diagnostic-a-w128-p128-d120-1792552215_1281224.json)).
Increasing only business workers to 256 lowered this to 35,702 but did not
meet the target
([JSON](benchmark-results/diagnostic-a-w256-p128-d120-1792552423_1285066.json)).
The measured average repository append time was about 18–20 ms across these
runs; because room operations hold a lock while awaiting each append, this
limits concurrency to one in-flight chat message per room. More workers can
reduce lock-wait starvation but cannot remove this per-room bound.

With 256 workers and eight writers, sending idle sockets immediately from the
Reactor cut 120-second unmatched sends from 35,702 to 25,327 and reduced mean
server CPU from 106.78% to 93.36%
([before](benchmark-results/diagnostic-a-w256-p128-d120-1792552423_1285066.json),
[after](benchmark-results/diagnostic-a-w256-p128-d120-1792552641_1288468.json)).
Changing the RoomManager's global read paths to shared mutexes was a regression:
57,270 unmatched and about 21.4 ms mean repository append time, so that change
was reverted
([JSON](benchmark-results/diagnostic-a-w256-p128-d120-1792552871_1292039.json)).

Increasing batch writers to 16 and reducing the collection window to 1 ms
was much worse (122,682 unmatched), so the Docker default returned to eight
writers, 32 rows, and 2 ms
([JSON](benchmark-results/diagnostic-a-w256-p128-d120-1792553140_1295627.json)).
The remaining per-room bottleneck was removed by taking a shared room-stripe
lock for chat while join/leave retain exclusive locks. This permits concurrent
chat writes from different connections in the same room; each connection still
uses one keyed executor lane. With 256 workers, 128 DB connections, eight
batch writers, and immediate Reactor sends, the 120-second A run persisted all
599,995 sends, delivered all 5,999,950 expected broadcasts, and had zero
errors/unmatched requests (P95 121.06 ms)
([JSON](benchmark-results/diagnostic-a-w256-p128-d120-1792553441_1299593.json)).
This is still a diagnostic; a fresh five-minute A result is required.

The follow-up five-minute A run **met** its specified throughput and integrity
checks: 1,499,994 sends persisted, 14,999,940 broadcasts delivered, zero
errors/unmatched, at 4,998.93 attempted msg/s. P95 self-echo was 6.88 s, so
the high-load responsiveness still needs work
([report](benchmark-a-validation.md),
[JSON](benchmark-results/scenario-a-1792553691_1304959.json)).
The same 256-worker/eight-writer build did **not** meet B's separate P99
latency goal: all 299,999 messages persisted and all broadcasts arrived, but
P99 was 2.10 s instead of below 100 ms
([report](benchmark-b-validation.md),
[JSON](benchmark-results/scenario-b-1792554068_1309582.json)).
Both formal results must be kept; the passing A result does not imply B passes.

## Five-minute B tail latency follow-up (2026-10-22)

The concurrent-chat build was tested with fewer workers and batch writers.
At 128 workers, 128 pooled connections and four batch writers, a 120-second B
diagnostic persisted all 119,999 sends and had 31.12 ms P99
([JSON](benchmark-results/diagnostic-b-w128-p128-d120-1792554476_1314362.json)).
That short result did not predict the five-minute run: the same profile
persisted all 299,999 sends with zero errors/unmatched requests, but P99 was
1.10 s ([JSON](benchmark-results/scenario-b-1792599318_1363940.json)).
The formal B target was **NOT MET on that four-writer profile**.

The 128-worker/four-writer profile also failed a 120-second A diagnostic:
1,854 unmatched sends and 598,987 message rows at the measurement point for
599,986 attempted sends
([JSON](benchmark-results/diagnostic-a-w128-p128-d120-1792598644_1352848.json)).
Changing only to 256 workers with four writers left 24,788 unmatched
([JSON](benchmark-results/diagnostic-a-w256-p128-d120-1792598829_1355841.json));
128 workers with eight writers left 2,738 unmatched
([JSON](benchmark-results/diagnostic-a-w128-p128-d120-1792599092_1360654.json)).
Neither is a passing A configuration.

An experimental switch restored exclusive, per-room chat locking while
keeping the newer Reactor path. Its 120-second B diagnostic was clean with
56.16 ms P99
([JSON](benchmark-results/diagnostic-b-w128-p128-d120-1792599795_1369208.json)),
but the five-minute B test again exceeded the limit at 1.28 s P99 despite
complete persistence and broadcast delivery
([JSON](benchmark-results/scenario-b-1792599963_1372306.json)). The switch
was therefore removed from the final code. Short diagnostics are insufficient
for claiming B latency; further work needs time-sliced latency and queue-depth
instrumentation to locate the long-tail pauses before another tuning change.

A fresh five-minute A validation on the final 256-worker/128-connection/eight-
writer configuration did meet throughput and integrity again. All 1,499,996
sent messages were persisted, all 14,999,960 broadcasts arrived, and there
were zero errors/unmatched requests. P99 self-echo was 113.92 ms
([report](benchmark-a-validation.md),
[JSON](benchmark-results/scenario-a-1792600479_1379996.json)).

The follow-up 128-worker/128-connection/**eight-writer** five-minute B run
did meet the separate persistence target: 299,999 sends persisted,
2,999,990 broadcasts arrived, zero errors/unmatched requests, and 60.28 ms
P99 self-echo against the strict 100 ms limit
([report](benchmark-b-validation.md),
[JSON](benchmark-results/scenario-b-1792600882_1384802.json)). The prior
256-worker/eight-writer B run failed at 2.10 s P99, so these results show
**two passing workload-specific profiles**, not one proven configuration for
both workloads. The combined report records each profile explicitly.
