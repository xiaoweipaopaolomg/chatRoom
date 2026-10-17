#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
trap 'docker compose logs server mysql || true' ERR
trap 'docker compose down' EXIT
docker compose up -d --build --wait server
python3 tests/e2e/chat_scenario.py
sleep 1
counts="$(docker compose exec -T mysql mysql -N -uroot \
  -p"${MYSQL_ROOT_PASSWORD:-root_dev_only}" chatroom -e \
  "SELECT (SELECT COUNT(*) FROM rooms),(SELECT COUNT(*) FROM membership_events WHERE event_type='JOIN'),(SELECT COUNT(*) FROM membership_events WHERE event_type='LEAVE'),(SELECT COUNT(*) FROM messages);")"
read -r rooms joins leaves messages <<<"$counts"
test "$rooms" -ge 1
test "$joins" -ge 3
test "$leaves" -ge 1
test "$messages" -ge 1
