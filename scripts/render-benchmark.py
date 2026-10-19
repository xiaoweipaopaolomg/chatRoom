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
database = data.get("database_verification")
database_ok = data.get("persistence") != "mysql" or (
    database is not None and database["messages_match_sent"]
)
met = (data.get("complete") is True and c["errors"] == 0 and
       c["unmatched"] == 0 and database_ok and
       data["actual_rate"] >= p["target_rate"] * 0.95)
report = f"""# Benchmark report

Status: **{'MET' if met else 'NOT MET'}**

This report is generated only from `{args.input}`.

| Metric | Value |
|---|---:|
| OS | {data['environment']['os']} |
| Logical CPUs | {data['environment']['logical_cpus']} |
| Persistence | {data.get('persistence', 'unspecified')} |
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
if database is not None:
    report += (
        f"| DB rooms added | {database['delta']['rooms']} |\n"
        f"| DB membership events added | {database['delta']['membership_events']} |\n"
        f"| DB messages added | {database['delta']['messages']} |\n"
        f"| DB messages match sent | {'yes' if database['messages_match_sent'] else 'no'} |\n"
    )
elif data.get("persistence") == "mysql":
    report += "| DB verification | missing |\n"
Path(args.output).write_text(report)
