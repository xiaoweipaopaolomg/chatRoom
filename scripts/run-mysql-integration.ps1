$ErrorActionPreference = "Stop"
try {
  docker compose -f docker-compose.test.yml up -d --wait mysql
  if ($LASTEXITCODE -ne 0) { throw "MySQL test container startup failed" }
  cmake --preset linux-debug
  if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
  cmake --build --preset linux-debug --target mysql_repository_test
  if ($LASTEXITCODE -ne 0) { throw "MySQL test build failed" }
  $testBinary = "./out/build/linux-debug/tests/mysql_repository_test"
  if ($env:OS -eq "Windows_NT") { $testBinary += ".exe" }
  & $testBinary
  if ($LASTEXITCODE -ne 0) { throw "MySQL repository tests failed" }
} finally { docker compose -f docker-compose.test.yml down -v }
