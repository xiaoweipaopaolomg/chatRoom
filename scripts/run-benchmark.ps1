param([switch]$Smoke, [switch]$FormalA, [switch]$FormalB)
$ErrorActionPreference = "Stop"
$selectedModes = @($Smoke, $FormalA, $FormalB) | Where-Object { $_ }
if (@($selectedModes).Count -gt 1) {
  throw "Choose at most one of -Smoke, -FormalA, or -FormalB"
}

cmake --preset linux-release
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
cmake --build --preset linux-release --target chatroom_load_client
if ($LASTEXITCODE -ne 0) { throw "Load client build failed" }

$commit = git --git-dir=.gitrepo rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw "Cannot determine Git commit" }
$env:CHATROOM_GIT_COMMIT = $commit.Trim()
$changes = git --git-dir=.gitrepo --work-tree=. status --porcelain
if ($LASTEXITCODE -ne 0) { throw "Cannot determine Git status" }
$env:CHATROOM_GIT_DIRTY = if ($changes) { "true" } else { "false" }
$env:CHATROOM_BENCHMARK_PERSISTENCE = "mysql"
$runId = "{0}_{1}" -f [DateTimeOffset]::UtcNow.ToUnixTimeSeconds(), $PID
$query = 'MYSQL_PWD="$MYSQL_PASSWORD" mysql -N -uchatroom chatroom -e "SELECT (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM membership_events), (SELECT COUNT(*) FROM messages);"'

function Get-Counts([string]$Project) {
  $value = docker compose -p $Project exec -T mysql sh -c $query
  if ($LASTEXITCODE -ne 0) { throw "Database count failed" }
  return ($value -join " ").Trim()
}

function Invoke-Scenario([string]$Label, [int]$Connections, [int]$Rooms,
                         [int]$Rate, [int]$Duration, [int]$Warmup, [string]$Output) {
  $project = "chatroom_bench_${runId}_${Label}"
  try {
    docker compose -p $project up -d --build --wait server
    if ($LASTEXITCODE -ne 0) { throw "Docker Compose startup failed" }

    $env:CHATROOM_BENCHMARK_SCENARIO = "warmup-$Label"
    ./out/build/linux-release/src/load_client/chatroom_load_client --host 127.0.0.1 --port 7777 `
      --connections 20 --rooms 4 --rate 100 --duration $Warmup `
      --output "docs/benchmark-results/warmup-$Label.json"
    if ($LASTEXITCODE -ne 0) { throw "Warmup failed" }
    Start-Sleep -Seconds 1

    $before = Get-Counts $project
    $env:CHATROOM_BENCHMARK_SCENARIO = $Label
    $serverId = docker compose -p $project ps -q server
    $mysqlId = docker compose -p $project ps -q mysql
    if (-not $serverId -or -not $mysqlId) { throw "Benchmark containers are missing" }
    $serverPid = docker inspect -f '{{.State.Pid}}' $serverId
    $mysqlPid = docker inspect -f '{{.State.Pid}}' $mysqlId
    $serverImage = docker inspect -f '{{.Image}}' $serverId
    $mysqlImage = docker inspect -f '{{.Image}}' $mysqlId
    $resources = $Output -replace '\.json$', '-resources.json'
    $sampler = Start-Process python -ArgumentList @('scripts/sample-resources.py',
      '--server-pid', $serverPid, '--mysql-pid', $mysqlPid,
      '--duration', $Duration, '--output', $resources) -PassThru -NoNewWindow
    ./out/build/linux-release/src/load_client/chatroom_load_client --host 127.0.0.1 --port 7777 `
      --connections $Connections --rooms $Rooms --rate $Rate --duration $Duration --output $Output
    $loadExit = $LASTEXITCODE
    $sampler.WaitForExit()
    if ($sampler.ExitCode -ne 0) { throw "Resource sampler failed" }
    $after = Get-Counts $project
    python scripts/verify-benchmark.py $Output $before $after `
      --resources $resources --server-image $serverImage --mysql-image $mysqlImage `
      --worker-count $(if ($env:CHATROOM_WORKER_COUNT) { $env:CHATROOM_WORKER_COUNT } else { 256 }) `
      --db-pool-size $(if ($env:CHATROOM_DB_POOL_SIZE) { $env:CHATROOM_DB_POOL_SIZE } else { 128 }) `
      --batch-writer-count $(if ($env:CHATROOM_BATCH_WRITER_COUNT) { $env:CHATROOM_BATCH_WRITER_COUNT } else { 8 })
    $verificationExit = $LASTEXITCODE
    if ($loadExit -ne 0) { throw "Load scenario $Label did not complete" }
    if ($verificationExit -ne 0) { throw "Database verification failed" }
  } catch {
    docker compose -p $project logs server mysql
    throw
  } finally {
    docker compose -p $project down -v
  }
}

$failed = $false
if ($FormalA) {
  $outputA = "docs/benchmark-results/scenario-a-$runId.json"
  try { Invoke-Scenario scenario-a 1000 100 5000 300 30 $outputA } catch { $failed = $true; Write-Warning $_ }
  if (Test-Path $outputA) {
    python scripts/render-benchmark.py --require-met $outputA docs/benchmark-a-validation.md
    if ($LASTEXITCODE -ne 0) { $failed = $true }
  } else { $failed = $true }
} elseif ($FormalB) {
  $outputB = "docs/benchmark-results/scenario-b-$runId.json"
  try { Invoke-Scenario scenario-b 500 50 1000 300 30 $outputB } catch { $failed = $true; Write-Warning $_ }
  if (Test-Path $outputB) {
    python scripts/render-benchmark.py --require-met $outputB docs/benchmark-b-validation.md
    if ($LASTEXITCODE -ne 0) { $failed = $true }
  } else { $failed = $true }
} elseif ($Smoke) {
  $outputA = "docs/benchmark-results/smoke-a-$runId.json"
  $outputB = "docs/benchmark-results/smoke-b-$runId.json"
  try { Invoke-Scenario smoke-a 20 4 100 10 2 $outputA } catch { $failed = $true; Write-Warning $_ }
  try { Invoke-Scenario smoke-b 20 4 50 10 2 $outputB } catch { $failed = $true; Write-Warning $_ }
} else {
  $outputA = "docs/benchmark-results/scenario-a-$runId.json"
  $outputB = "docs/benchmark-results/scenario-b-$runId.json"
  try { Invoke-Scenario scenario-a 1000 100 5000 300 30 $outputA } catch { $failed = $true; Write-Warning $_ }
  try { Invoke-Scenario scenario-b 500 50 1000 300 30 $outputB } catch { $failed = $true; Write-Warning $_ }
}
if (-not $FormalA -and -not $FormalB) {
  if ((Test-Path $outputA) -and (Test-Path $outputB)) {
    python scripts/render-benchmark.py --require-met $outputA $outputB docs/benchmark.md
    if ($LASTEXITCODE -ne 0) { $failed = $true }
  } else {
    $failed = $true
  }
}
if ($failed) { throw "One or more benchmark scenarios failed; inspect the raw JSON and Compose logs" }
