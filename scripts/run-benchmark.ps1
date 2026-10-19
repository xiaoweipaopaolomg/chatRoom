param([switch]$Smoke)
$ErrorActionPreference = "Stop"
$connections=1000; $rooms=100; $rate=5000; $duration=300
if ($Smoke) { $connections=20; $rooms=4; $rate=100; $duration=10 }
$commit = git rev-parse HEAD 2>$null
if ($LASTEXITCODE -ne 0) { $commit = git --git-dir=.gitrepo rev-parse HEAD }
if ($LASTEXITCODE -ne 0) { throw "Cannot determine Git commit" }
$env:CHATROOM_GIT_COMMIT = $commit.Trim()
$env:CHATROOM_BENCHMARK_PERSISTENCE = "mysql"
cmake --preset linux-release
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
cmake --build --preset linux-release --target chatroom_load_client
if ($LASTEXITCODE -ne 0) { throw "Load client build failed" }
try {
  docker compose up -d --build --wait server
  if ($LASTEXITCODE -ne 0) { throw "Docker Compose startup failed" }
  $query = 'MYSQL_PWD="$MYSQL_PASSWORD" mysql -N -uchatroom chatroom -e "SELECT (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM membership_events), (SELECT COUNT(*) FROM messages);"'
  $beforeCounts = docker compose exec -T mysql sh -c $query
  if ($LASTEXITCODE -ne 0) { throw "Initial database count failed" }
  ./out/build/linux-release/src/load_client/chatroom_load_client --host 127.0.0.1 --port 7777 --connections $connections --rooms $rooms --rate $rate --duration $duration --output docs/benchmark-results/latest.json
  if ($LASTEXITCODE -ne 0) { throw "Load client failed" }
  $afterCounts = docker compose exec -T mysql sh -c $query
  if ($LASTEXITCODE -ne 0) { throw "Final database count failed" }
  python scripts/verify-benchmark.py docs/benchmark-results/latest.json "$beforeCounts" "$afterCounts"
  $verificationExit = $LASTEXITCODE
  python scripts/render-benchmark.py docs/benchmark-results/latest.json docs/benchmark.md
  if ($LASTEXITCODE -ne 0) { throw "Benchmark report generation failed" }
  if ($verificationExit -ne 0) { throw "Database row-count verification failed" }
} finally { docker compose down }
