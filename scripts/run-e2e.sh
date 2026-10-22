#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"
project="chatroom_e2e_$(date +%s)_$$"
trap 'docker compose -p "$project" logs server mysql || true' ERR
trap 'docker compose -p "$project" down -v' EXIT
docker compose -p "$project" up -d --build --wait server
deadline=$((SECONDS + 45))
until docker compose -p "$project" logs --no-color server 2>/dev/null |
  grep -q '"event":"server.started"'; do
  if ((SECONDS >= deadline)); then
    echo "server did not log startup within 45 seconds" >&2
    exit 1
  fi
  sleep 0.2
done
db_counts() {
  docker compose -p "$project" exec -T mysql sh -c \
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
server_id="$(docker compose -p "$project" ps -q server)"
test -n "$server_id"
shutdown_start="$(date +%s)"
docker compose -p "$project" stop -t 10 server
test "$(( $(date +%s) - shutdown_start ))" -le 12
test "$(docker inspect -f '{{.State.ExitCode}}' "$server_id")" -eq 0
server_logs="$(docker compose -p "$project" logs --no-color server)"
[[ "$server_logs" == *'"event":"server.stopped"'* ]]
[[ "$server_logs" == *'"event":"server.mysql_message_metrics"'* ]]
