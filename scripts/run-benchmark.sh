#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
connections=1000; rooms=100; rate=5000; duration=300
if [[ "${1:-}" == "--smoke" ]]; then connections=20; rooms=4; rate=100; duration=10; fi
cmake --preset linux-release
cmake --build --preset linux-release --target chatroom_load_client
docker compose up -d --build --wait server
trap 'docker compose down' EXIT
output="docs/benchmark-results/latest.json"
export CHATROOM_GIT_COMMIT="$(git rev-parse HEAD 2>/dev/null || git --git-dir=.gitrepo rev-parse HEAD 2>/dev/null || echo unknown)"
export CHATROOM_BENCHMARK_PERSISTENCE=mysql
out/build/linux-release/src/load_client/chatroom_load_client --host 127.0.0.1 --port 7777 \
  --connections "$connections" --rooms "$rooms" --rate "$rate" --duration "$duration" --output "$output"
python3 scripts/render-benchmark.py "$output" docs/benchmark.md
