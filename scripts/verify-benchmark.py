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

temporary = result_path.with_name(result_path.name + ".tmp")
temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
os.replace(temporary, result_path)

if not matches_sent or any(count < 0 for count in delta.values()):
    raise SystemExit("database row-count delta does not match the load-client result")
