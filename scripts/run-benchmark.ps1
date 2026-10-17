param([switch]$Smoke)
$ErrorActionPreference = "Stop"
$connections=1000; $rooms=100; $rate=5000; $duration=300
if ($Smoke) { $connections=20; $rooms=4; $rate=100; $duration=10 }
cmake --preset linux-release
cmake --build --preset linux-release --target chatroom_load_client
docker compose up -d --build --wait server
try {
  ./out/build/linux-release/src/load_client/chatroom_load_client --host 127.0.0.1 --port 7777 --connections $connections --rooms $rooms --rate $rate --duration $duration --output docs/benchmark-results/latest.json
  python scripts/render-benchmark.py docs/benchmark-results/latest.json docs/benchmark.md
} finally { docker compose down }
