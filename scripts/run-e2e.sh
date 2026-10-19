#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
trap 'docker compose logs server mysql || true' ERR
trap 'docker compose down' EXIT
docker compose up -d --build --wait server
db_counts() {
  docker compose exec -T mysql sh -c \
    'MYSQL_PWD="$MYSQL_PASSWORD" mysql -N -uchatroom chatroom -e "$1"' sh \
    "SELECT (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM membership_events WHERE event_type='JOIN'), (SELECT COUNT(*) FROM membership_events WHERE event_type='LEAVE'), (SELECT COUNT(*) FROM messages);"
}
before_counts="$(db_counts)"
read -r rooms_before joins_before leaves_before messages_before <<<"$before_counts"
python3 tests/e2e/chat_scenario.py
sleep 1
counts="$(db_counts)"
read -r rooms joins leaves messages <<<"$counts"
test "$((rooms - rooms_before))" -eq 1
test "$((joins - joins_before))" -eq 3
test "$((leaves - leaves_before))" -ge 1
test "$((messages - messages_before))" -eq 1
