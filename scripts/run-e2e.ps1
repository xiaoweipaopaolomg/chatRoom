$ErrorActionPreference = "Stop"
try {
  docker compose up -d --build --wait server
  if ($LASTEXITCODE -ne 0) { throw "Docker Compose startup failed" }
  $query = "SELECT (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM membership_events WHERE event_type='JOIN'), (SELECT COUNT(*) FROM membership_events WHERE event_type='LEAVE'), (SELECT COUNT(*) FROM messages);"
  $dbCommand = 'MYSQL_PWD="$MYSQL_PASSWORD" mysql -N -uchatroom chatroom -e "$1"'
  $before = docker compose exec -T mysql sh -c $dbCommand sh $query
  if ($LASTEXITCODE -ne 0) { throw "Initial database count failed" }

  python tests/e2e/chat_scenario.py
  if ($LASTEXITCODE -ne 0) { throw "Three-client E2E scenario failed" }
  Start-Sleep -Seconds 1

  $after = docker compose exec -T mysql sh -c $dbCommand sh $query
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
} finally { docker compose down }
