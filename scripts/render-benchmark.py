#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("input")
parser.add_argument("output")
args = parser.parse_args()
data = json.loads(Path(args.input).read_text())
p = data["parameters"]
c = data["counts"]
l = data["latency_us"]
met = c["errors"] == 0 and data["actual_rate"] >= p["target_rate"] * 0.95
report = f"""# Benchmark report

Status: **{'MET' if met else 'NOT MET'}**

This report is generated only from `{args.input}`.

| Metric | Value |
|---|---:|
| OS | {data['environment']['os']} |
| Logical CPUs | {data['environment']['logical_cpus']} |
| Connections | {p['connections']} |
| Rooms | {p['rooms']} |
| Duration | {p['duration_seconds']} s |
| Target rate | {p['target_rate']:.2f} msg/s |
| Actual rate | {data['actual_rate']:.2f} msg/s |
| Sent | {c['sent']} |
| Broadcast deliveries | {c['broadcasts']} |
| Errors | {c['errors']} |
| Unmatched | {c['unmatched']} |
| P50 | {l['p50']} µs |
| P95 | {l['p95']} µs |
| P99 | {l['p99']} µs |
"""
Path(args.output).write_text(report)
