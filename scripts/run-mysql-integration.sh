#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
project="chatroom_mysql_test_$(date +%s)_$$"
cleanup() { docker compose -p "$project" -f docker-compose.test.yml down -v; }
trap 'docker compose -p "$project" -f docker-compose.test.yml logs mysql || true' ERR
trap cleanup EXIT
docker compose -p "$project" -f docker-compose.test.yml up -d --wait mysql
cmake --preset linux-debug
cmake --build --preset linux-debug --target mysql_repository_test
out/build/linux-debug/tests/mysql_repository_test
