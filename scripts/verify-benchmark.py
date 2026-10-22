#!/usr/bin/env python3
"""Attach observed MySQL row-count deltas to a load-client result."""

import argparse
import json
import os
from pathlib import Path


def parse_counts(value):
    parts = value.split()
    if len(parts) != 3:
        raise ValueError("expected room, membership, and message counts")
    counts = dict(zip(("rooms", "membership_events", "messages"), map(int, parts)))
    if any(count < 0 for count in counts.values()):
        raise ValueError("database counts must be non-negative")
    return counts


parser = argparse.ArgumentParser()
parser.add_argument("result")
parser.add_argument("before")
parser.add_argument("after")
parser.add_argument("--resources")
parser.add_argument("--server-image")
parser.add_argument("--mysql-image")
parser.add_argument("--worker-count", type=int)
parser.add_argument("--db-pool-size", type=int)
parser.add_argument("--batch-writer-count", type=int)
args = parser.parse_args()

result_path = Path(args.result)
result = json.loads(result_path.read_text(encoding="utf-8"))
before = parse_counts(args.before)
after = parse_counts(args.after)
delta = {key: after[key] - before[key] for key in before}
matches_sent = delta["messages"] == result["counts"]["sent"]
result["database_verification"] = {
    "before": before,
    "after": after,
    "delta": delta,
    "messages_match_sent": matches_sent,
}
if args.resources:
    resources = json.loads(Path(args.resources).read_text(encoding="utf-8"))
    result["resources"] = {
        "host_ram_bytes": resources["host_ram_bytes"],
        "summary": resources["summary"],
        "samples_file": args.resources,
    }
if args.server_image:
    result.setdefault("environment", {})["server_image_id"] = args.server_image
if args.mysql_image:
    result.setdefault("environment", {})["mysql_image_id"] = args.mysql_image
if any(value is not None for value in (
    args.worker_count, args.db_pool_size, args.batch_writer_count,
)):
    result["server_configuration"] = {
        "worker_count": args.worker_count,
        "db_pool_size": args.db_pool_size,
        "batch_writer_count": args.batch_writer_count,
    }

temporary = result_path.with_name(result_path.name + ".tmp")
temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
os.replace(temporary, result_path)

if not matches_sent or any(count < 0 for count in delta.values()):
    raise SystemExit("database row-count delta does not match the load-client result")
