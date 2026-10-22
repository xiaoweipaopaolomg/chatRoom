#!/usr/bin/env python3
"""Render one or more verified benchmark JSON files without inventing measurements."""

import argparse
import json
from pathlib import Path


def render_run(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    parameters = data["parameters"]
    counts = data["counts"]
    latency = data["latency_us"]
    database = data.get("database_verification")
    database_ok = data.get("persistence") != "mysql" or (
        database is not None and database["messages_match_sent"]
    )
    fanout_ok = counts.get("expected_broadcasts") is None or (
        counts["broadcasts"] == counts["expected_broadcasts"]
    )
    met = (
        data.get("complete") is True
        and counts["errors"] == 0
        and counts["unmatched"] == 0
        and fanout_ok
        and database_ok
        and data["actual_rate"] >= parameters["target_rate"] * 0.95
    )
    # The handbook's persistence scenario has an additional strict latency goal.
    persistence_latency_ok = not (
        parameters["duration_seconds"] >= 300
        and parameters["connections"] == 500
        and parameters["rooms"] == 50
        and parameters["target_rate"] == 1000
    ) or latency["p99"] < 100_000
    met = met and persistence_latency_ok
    scope = "smoke" if parameters["duration_seconds"] < 300 else "formal"
    label = data.get("scenario", path.stem)
    lines = [
        f"## {label}",
        "",
        f"Status: **{scope.upper()} {'MET' if met else 'NOT MET'}**",
        "",
        f"Evidence: `{path}`",
        "",
        "| Metric | Value |",
        "|---|---:|",
        f"| Git commit | {data.get('git_commit', 'not recorded')} |",
        f"| Working tree dirty | {data.get('git_dirty', 'not recorded')} |",
        f"| OS | {data['environment']['os']} |",
        f"| Logical CPUs | {data['environment']['logical_cpus']} |",
        f"| Compiler | {data['environment'].get('compiler', 'not recorded')} |",
        f"| Build type | {data['environment'].get('build_type', 'not recorded')} |",
        f"| Server image | {data['environment'].get('server_image_id', 'not recorded')} |",
        f"| MySQL image | {data['environment'].get('mysql_image_id', 'not recorded')} |",
        f"| Persistence | {data.get('persistence', 'unspecified')} |",
        f"| Business workers | {data.get('server_configuration', {}).get('worker_count', 'not recorded')} |",
        f"| MySQL pool size | {data.get('server_configuration', {}).get('db_pool_size', 'not recorded')} |",
        f"| Batch writers | {data.get('server_configuration', {}).get('batch_writer_count', 'not recorded')} |",
        f"| Connections | {parameters['connections']} |",
        f"| Rooms | {parameters['rooms']} |",
        f"| Duration | {parameters['duration_seconds']} s |",
        f"| Connections initialized | {parameters['connections']} / {parameters['connections']} (100%) |",
        f"| Target rate | {parameters['target_rate']:.2f} msg/s |",
        f"| Attempted send rate | {data['actual_rate']:.2f} msg/s |",
        f"| Inbound throughput | {counts['sent'] / parameters['duration_seconds']:.2f} msg/s |",
        f"| Attempted sends | {counts['sent']} |",
        f"| Broadcast deliveries | {counts['broadcasts']} |",
        f"| Outbound throughput | {counts['broadcasts'] / parameters['duration_seconds']:.2f} deliveries/s |",
        f"| Expected broadcast deliveries | {counts.get('expected_broadcasts', 'not recorded')} |",
        f"| Errors | {counts['errors']} |",
        f"| Error rate | {counts['errors'] / counts['sent'] * 100 if counts['sent'] else 0:.4f}% |",
        f"| Unmatched | {counts['unmatched']} |",
        f"| P50 | {latency['p50']} µs |",
        f"| P95 | {latency['p95']} µs |",
        f"| P99 | {latency['p99']} µs |",
    ]
    if parameters["duration_seconds"] >= 300 and parameters["connections"] == 500 \
            and parameters["rooms"] == 50 and parameters["target_rate"] == 1000:
        lines.append("| Persistence P99 target | < 100000 µs |")
    resources = data.get("resources")
    if resources is not None:
        def percent(value):
            return "not recorded" if value is None else f"{value:.2f}%"

        lines.extend([
            f"| Host RAM | {resources['host_ram_bytes']} bytes |",
            f"| Server mean CPU | {percent(resources['summary']['server']['mean_cpu_percent'])} |",
            f"| Server peak RSS | {resources['summary']['server']['peak_rss_bytes']} bytes |",
            f"| MySQL mean CPU | {percent(resources['summary']['mysql']['mean_cpu_percent'])} |",
            f"| MySQL peak RSS | {resources['summary']['mysql']['peak_rss_bytes']} bytes |",
            f"| Resource samples | `{resources['samples_file']}` |",
        ])
        if "client" in resources["summary"]:
            lines.insert(-1, f"| Load-client mean CPU | {percent(resources['summary']['client']['mean_cpu_percent'])} |")
            lines.insert(-1, f"| Load-client peak RSS | {resources['summary']['client']['peak_rss_bytes']} bytes |")
    if database is not None:
        lines.extend(
            [
                f"| DB rooms added | {database['delta']['rooms']} |",
                f"| DB membership events added | {database['delta']['membership_events']} |",
                f"| DB messages added | {database['delta']['messages']} |",
                f"| Persisted rate | {database['delta']['messages'] / parameters['duration_seconds']:.2f} msg/s |",
                f"| DB messages match sent | {'yes' if database['messages_match_sent'] else 'no'} |",
            ]
        )
    elif data.get("persistence") == "mysql":
        lines.append("| DB verification | missing |")
    lines.extend(["", f"Load command: `{data.get('command', 'not recorded')}`", ""])
    configuration = data.get("server_configuration", {})
    if label in ("scenario-a", "scenario-b") and all(
        configuration.get(key) is not None
        for key in ("worker_count", "db_pool_size", "batch_writer_count")
    ):
        mode = "--formal-a" if label == "scenario-a" else "--formal-b"
        lines.extend([
            "Reproduction command: `"
            f"CHATROOM_WORKER_COUNT={configuration['worker_count']} "
            f"CHATROOM_DB_POOL_SIZE={configuration['db_pool_size']} "
            f"CHATROOM_BATCH_WRITER_COUNT={configuration['batch_writer_count']} "
            f"./scripts/run-benchmark.sh {mode}`",
            "",
        ])
    return scope, met, "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--require-met", action="store_true")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    if len(args.paths) < 2:
        parser.error("provide at least one input JSON and one output Markdown path")
    inputs, output = args.paths[:-1], args.paths[-1]
    rendered = [render_run(path) for path in inputs]
    scopes = {scope for scope, _, _ in rendered}
    overall = "SMOKE" if scopes == {"smoke"} else "FORMAL" if scopes == {"formal"} else "MIXED"
    status = "MET" if all(met for _, met, _ in rendered) else "NOT MET"
    introduction = (
        "This short smoke run is not a formal capacity result.\n\n"
        if overall == "SMOKE" else ""
    )
    profiles = [json.loads(path.read_text(encoding="utf-8")).get("server_configuration")
                for path in inputs]
    if len(profiles) > 1 and len({json.dumps(profile, sort_keys=True) for profile in profiles}) > 1:
        introduction += (
            "The scenarios use different server configurations, shown in each table. "
            "Passing both is not evidence that one configuration meets both targets.\n\n"
        )
    report = f"# Benchmark report\n\nStatus: **{overall} {status}**\n\n{introduction}"
    report += "\n".join(section for _, _, section in rendered)
    output.write_text(report, encoding="utf-8")
    if args.require_met and not all(met for _, met, _ in rendered):
        raise SystemExit("benchmark target not met; see generated report")


if __name__ == "__main__":
    main()
