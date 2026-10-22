#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
if [[ $# -gt 1 || ( $# -eq 1 && "$1" != "--smoke" && "$1" != "--diagnostic" && "$1" != "--diagnostic-a" && "$1" != "--formal-a" && "$1" != "--formal-b" ) ]]; then
  echo "usage: $0 [--smoke|--diagnostic|--diagnostic-a|--formal-a|--formal-b]" >&2
  exit 2
fi
smoke=false
if [[ "${1:-}" == "--smoke" ]]; then smoke=true; fi
diagnostic=false
if [[ "${1:-}" == "--diagnostic" || "${1:-}" == "--diagnostic-a" ]]; then diagnostic=true; fi
diagnostic_duration="${CHATROOM_DIAGNOSTIC_DURATION:-30}"
if ! [[ "$diagnostic_duration" =~ ^[1-9][0-9]*$ ]] ||
   ((diagnostic_duration < 1 || diagnostic_duration > 300)); then
  echo "CHATROOM_DIAGNOSTIC_DURATION must be in 1..300" >&2
  exit 2
fi

cmake --preset linux-release
cmake --build --preset linux-release --target chatroom_load_client
export CHATROOM_GIT_COMMIT="$(git --git-dir=.gitrepo rev-parse HEAD)"
export CHATROOM_GIT_DIRTY=false
if [[ -n "$(git --git-dir=.gitrepo --work-tree=. status --porcelain)" ]]; then
  export CHATROOM_GIT_DIRTY=true
fi
export CHATROOM_BENCHMARK_PERSISTENCE=mysql

run_id="$(date +%s)_$$"
project=""
cleanup() {
  if [[ -n "$project" ]]; then
    docker compose -p "$project" down -v
    project=""
  fi
}
on_error() {
  if [[ -n "$project" ]]; then docker compose -p "$project" logs server mysql || true; fi
}
trap on_error ERR
trap cleanup EXIT

db_counts() {
  docker compose -p "$project" exec -T mysql sh -c \
    'MYSQL_PWD="$MYSQL_PASSWORD" mysql -N -uchatroom chatroom -e "SELECT (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM membership_events), (SELECT COUNT(*) FROM messages);"'
}

wait_for_server() {
  local deadline=$((SECONDS + 45))
  until docker compose -p "$project" logs --no-color server 2>/dev/null |
    grep -q '"event":"server.started"'; do
    if ((SECONDS >= deadline)); then
      echo "server did not log startup within 45 seconds" >&2
      return 1
    fi
    sleep 0.2
  done
  python3 -c '
import socket
import time
deadline = time.monotonic() + 45
while time.monotonic() < deadline:
    try:
        with socket.create_connection(("127.0.0.1", 7777), timeout=1):
            break
    except OSError:
        time.sleep(0.2)
else:
    raise SystemExit("server did not accept connections within 45 seconds")
'
}

run_scenario() {
  local label="$1" connections="$2" rooms="$3" rate="$4" duration="$5" warmup="$6" output="$7"
  project="chatroom_bench_${run_id}_${label}"
  if ! docker compose -p "$project" up -d --build --wait server; then
    on_error; cleanup; return 1
  fi
  if ! wait_for_server; then on_error; cleanup; return 1; fi
  export CHATROOM_BENCHMARK_SCENARIO="warmup-${label}"
  if ! out/build/linux-release/src/load_client/chatroom_load_client \
    --host 127.0.0.1 --port 7777 --connections 20 --rooms 4 --rate 100 \
    --duration "$warmup" --output "docs/benchmark-results/warmup-${label}.json"; then
    on_error; cleanup; return 1
  fi
  sleep 1
  local before_counts after_counts
  if ! before_counts="$(db_counts)"; then on_error; cleanup; return 1; fi
  export CHATROOM_BENCHMARK_SCENARIO="$label"
  local server_id mysql_id server_pid mysql_pid server_image mysql_image resources_file server_log_file mysql_log_file
  server_id="$(docker compose -p "$project" ps -q server)"
  mysql_id="$(docker compose -p "$project" ps -q mysql)"
  server_pid="$(docker inspect -f '{{.State.Pid}}' "$server_id")"
  mysql_pid="$(docker inspect -f '{{.State.Pid}}' "$mysql_id")"
  server_image="$(docker inspect -f '{{.Image}}' "$server_id")"
  mysql_image="$(docker inspect -f '{{.Image}}' "$mysql_id")"
  resources_file="${output%.json}-resources.json"
  server_log_file="${output%.json}-server.log"
  mysql_log_file="${output%.json}-mysql.log"
  local load_status=0 verification_status=0 sampler_status=0
  out/build/linux-release/src/load_client/chatroom_load_client \
    --host 127.0.0.1 --port 7777 --connections "$connections" --rooms "$rooms" \
    --rate "$rate" --duration "$duration" --output "$output" &
  local load_pid=$!
  python3 scripts/sample-resources.py --server-pid "$server_pid" --mysql-pid "$mysql_pid" \
    --client-pid "$load_pid" --duration "$duration" --output "$resources_file" &
  local sampler_pid=$!
  wait "$load_pid" || load_status=$?
  wait "$sampler_pid" || sampler_status=$?
  if ! after_counts="$(db_counts)"; then on_error; cleanup; return 1; fi
  if [[ -f "$output" && -f "$resources_file" ]]; then
    python3 scripts/verify-benchmark.py "$output" "$before_counts" "$after_counts" \
      --resources "$resources_file" --server-image "$server_image" \
      --mysql-image "$mysql_image" \
      --worker-count "${CHATROOM_WORKER_COUNT:-256}" \
      --db-pool-size "${CHATROOM_DB_POOL_SIZE:-128}" \
      --batch-writer-count "${CHATROOM_BATCH_WRITER_COUNT:-8}" || verification_status=$?
  else
    verification_status=1
  fi
  if ! docker compose -p "$project" stop -t 90 server; then
    verification_status=1
  fi
  if ! docker compose -p "$project" logs --no-color server > "$server_log_file"; then
    verification_status=1
  fi
  if ((load_status != 0 || sampler_status != 0 || verification_status != 0)); then
    docker compose -p "$project" logs --no-color mysql > "$mysql_log_file" || true
  fi
  cleanup
  if ((load_status != 0)); then return "$load_status"; fi
  if ((sampler_status != 0)); then return "$sampler_status"; fi
  return "$verification_status"
}

overall_status=0
if "$diagnostic"; then
  workers="${CHATROOM_WORKER_COUNT:-256}"
  pool="${CHATROOM_DB_POOL_SIZE:-128}"
  if [[ "${1:-}" == "--diagnostic-a" ]]; then
    result="docs/benchmark-results/diagnostic-a-w${workers}-p${pool}-d${diagnostic_duration}-${run_id}.json"
    run_scenario "diagnostic-a-w${workers}-p${pool}-d${diagnostic_duration}" 1000 100 5000 "$diagnostic_duration" 3 "$result" || overall_status=1
  else
    result="docs/benchmark-results/diagnostic-b-w${workers}-p${pool}-d${diagnostic_duration}-${run_id}.json"
    run_scenario "diagnostic-b-w${workers}-p${pool}-d${diagnostic_duration}" 500 50 1000 "$diagnostic_duration" 3 "$result" || overall_status=1
  fi
  echo "Diagnostic result: $result"
elif [[ "${1:-}" == "--formal-a" ]]; then
  result="docs/benchmark-results/scenario-a-${run_id}.json"
  run_scenario scenario-a 1000 100 5000 300 30 "$result" || overall_status=1
  python3 scripts/render-benchmark.py --require-met "$result" docs/benchmark-a-validation.md || overall_status=1
elif [[ "${1:-}" == "--formal-b" ]]; then
  result="docs/benchmark-results/scenario-b-${run_id}.json"
  run_scenario scenario-b 500 50 1000 300 30 "$result" || overall_status=1
  python3 scripts/render-benchmark.py --require-met "$result" docs/benchmark-b-validation.md || overall_status=1
elif "$smoke"; then
  result_a="docs/benchmark-results/smoke-a-${run_id}.json"
  result_b="docs/benchmark-results/smoke-b-${run_id}.json"
  run_scenario smoke-a 20 4 100 10 2 "$result_a" || overall_status=1
  run_scenario smoke-b 20 4 50 10 2 "$result_b" || overall_status=1
  python3 scripts/render-benchmark.py --require-met "$result_a" "$result_b" docs/benchmark.md || overall_status=1
else
  result_a="docs/benchmark-results/scenario-a-${run_id}.json"
  result_b="docs/benchmark-results/scenario-b-${run_id}.json"
  run_scenario scenario-a 1000 100 5000 300 30 "$result_a" || overall_status=1
  run_scenario scenario-b 500 50 1000 300 30 "$result_b" || overall_status=1
  python3 scripts/render-benchmark.py --require-met "$result_a" "$result_b" docs/benchmark.md || overall_status=1
fi
exit "$overall_status"
