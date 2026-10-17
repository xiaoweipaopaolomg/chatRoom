#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
cleanup() { docker compose -f docker-compose.test.yml down -v; }
trap 'docker compose -f docker-compose.test.yml logs mysql || true' ERR
trap cleanup EXIT
docker compose -f docker-compose.test.yml up -d --wait mysql
cmake --preset linux-debug
cmake --build --preset linux-debug --target mysql_repository_test
ctest --preset linux-debug -R mysql_repository_test --output-on-failure --include-disabled
