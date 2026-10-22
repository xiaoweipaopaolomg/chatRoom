$ErrorActionPreference = "Stop"
$project = "chatroom_e2e_$(Get-Date -Format yyyyMMddHHmmss)_$PID"
try {
  docker compose -p $project up -d --build --wait server
  if ($LASTEXITCODE -ne 0) { throw "Docker Compose startup failed" }
  $ready = $false
  for ($attempt = 0; $attempt -lt 225; $attempt++) {
    $startupLogs = docker compose -p $project logs --no-color server 2>$null
    if ($startupLogs -match '"event":"server.started"') { $ready = $true; break }
    Start-Sleep -Milliseconds 200
  }
  if (-not $ready) { throw "Server did not log startup within 45 seconds" }
  $query = "SELECT (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM membership_events WHERE event_type='JOIN'), (SELECT COUNT(*) FROM membership_events WHERE event_type='LEAVE'), (SELECT COUNT(*) FROM messages);"
  $dbCommand = 'MYSQL_PWD="$MYSQL_PASSWORD" mysql -N -uchatroom chatroom -e "$1"'
  $before = docker compose -p $project exec -T mysql sh -c $dbCommand sh $query
  if ($LASTEXITCODE -ne 0) { throw "Initial database count failed" }

  python tests/e2e/chat_scenario.py
  if ($LASTEXITCODE -ne 0) { throw "Three-client E2E scenario failed" }
  Start-Sleep -Seconds 1

  $after = docker compose -p $project exec -T mysql sh -c $dbCommand sh $query
  if ($LASTEXITCODE -ne 0) { throw "Final database count failed" }
  $beforeValues = @($before.Trim() -split '\s+' | ForEach-Object { [long]$_ })
  $afterValues = @($after.Trim() -split '\s+' | ForEach-Object { [long]$_ })
  if ($beforeValues.Count -ne 4 -or $afterValues.Count -ne 4) {
    throw "Unexpected database count format"
  }
  if (($afterValues[0] - $beforeValues[0]) -ne 1 -or
      ($afterValues[1] - $beforeValues[1]) -ne 3 -or
      ($afterValues[2] - $beforeValues[2]) -lt 1 -or
      ($afterValues[3] - $beforeValues[3]) -ne 1) {
    throw "E2E database row-count delta did not match the scenario"
  }
  $serverId = docker compose -p $project ps -q server
  if ($LASTEXITCODE -ne 0 -or -not $serverId) { throw "Cannot identify server container" }
  $started = Get-Date
  docker compose -p $project stop -t 10 server
  if ($LASTEXITCODE -ne 0) { throw "Server shutdown failed" }
  if (((Get-Date) - $started).TotalSeconds -gt 12) { throw "Server shutdown exceeded timeout" }
  $exitCode = docker inspect -f '{{.State.ExitCode}}' $serverId
  if ($LASTEXITCODE -ne 0 -or [int]$exitCode -ne 0) { throw "Server did not exit cleanly" }
  $serverLogs = docker compose -p $project logs --no-color server
  if ($LASTEXITCODE -ne 0 -or -not ($serverLogs -match '"event":"server.stopped"')) {
    throw "Server stopped log entry missing"
  }
  if (-not ($serverLogs -match '"event":"server.mysql_message_metrics"')) {
    throw "MySQL message metrics log entry missing"
  }
} finally { docker compose -p $project down -v }
