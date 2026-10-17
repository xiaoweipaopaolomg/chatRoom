$ErrorActionPreference = "Stop"
docker compose up -d --build --wait server
try { python tests/e2e/chat_scenario.py } finally { docker compose down }
