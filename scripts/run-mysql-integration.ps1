$ErrorActionPreference = "Stop"
docker compose -f docker-compose.test.yml up -d --wait mysql
try {
  cmake --preset linux-debug
  cmake --build --preset linux-debug --target mysql_repository_test
  ctest --preset linux-debug -R mysql_repository_test --output-on-failure --include-disabled
} finally { docker compose -f docker-compose.test.yml down -v }
