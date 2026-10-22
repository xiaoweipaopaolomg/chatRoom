#!/usr/bin/env python3
"""Sample host-visible CPU and RSS for benchmark processes."""

import argparse
import json
import os
import time
from pathlib import Path


def read_process(pid):
    stat = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    fields = stat[stat.rfind(")") + 2 :].split()
    cpu_ticks = int(fields[11]) + int(fields[12])
    pages = int(fields[21])
    return cpu_ticks, pages * os.sysconf("SC_PAGE_SIZE")


def read_ram_bytes():
    for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
        if line.startswith("MemTotal:"):
            return int(line.split()[1]) * 1024
    raise RuntimeError("MemTotal missing from /proc/meminfo")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-pid", type=int, required=True)
    parser.add_argument("--mysql-pid", type=int, required=True)
    parser.add_argument("--client-pid", type=int)
    parser.add_argument("--duration", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if min(args.server_pid, args.mysql_pid, args.duration) <= 0 or (
        args.client_pid is not None and args.client_pid <= 0
    ):
        parser.error("PIDs and duration must be positive")

    ticks_per_second = os.sysconf("SC_CLK_TCK")
    previous = {}
    processes = {"server": args.server_pid, "mysql": args.mysql_pid}
    if args.client_pid is not None:
        processes["client"] = args.client_pid
    samples = {label: [] for label in processes}
    started = time.monotonic()
    deadline = started + args.duration
    while time.monotonic() < deadline:
        now = time.monotonic()
        for label, pid in processes.items():
            try:
                ticks, rss = read_process(pid)
            except (FileNotFoundError, ProcessLookupError):
                samples[label].append({"elapsed_seconds": now - started, "missing": True})
                continue
            last = previous.get(label)
            cpu_percent = None
            if last is not None and now > last[0]:
                cpu_percent = 100.0 * (ticks - last[1]) / ticks_per_second / (now - last[0])
            samples[label].append({"elapsed_seconds": now - started, "cpu_percent": cpu_percent,
                                   "rss_bytes": rss})
            previous[label] = (now, ticks)
        time.sleep(min(1.0, max(0.0, deadline - time.monotonic())))

    summary = {}
    for label, values in samples.items():
        valid = [value for value in values if "rss_bytes" in value]
        cpu = [value["cpu_percent"] for value in valid if value["cpu_percent"] is not None]
        summary[label] = {
            "samples": len(valid),
            "peak_rss_bytes": max((value["rss_bytes"] for value in valid), default=None),
            "mean_cpu_percent": sum(cpu) / len(cpu) if cpu else None,
            "peak_cpu_percent": max(cpu, default=None),
        }
    result = {"host_ram_bytes": read_ram_bytes(), "summary": summary, "samples": samples}
    temporary = args.output.with_name(args.output.name + ".tmp")
    temporary.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, args.output)


if __name__ == "__main__":
    main()
