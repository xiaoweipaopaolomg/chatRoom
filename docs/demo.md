# Demo checklist

1. Start the stack with `docker compose up -d --build --wait server`.
2. Open two `chatroom_qt_client` windows and connect both to `127.0.0.1:7777`
   with different nicknames.
3. Create a room in the first window; join its numeric id from the second.
4. Send plain-text messages in both directions and confirm both member lists.
5. Close one client and confirm `member.left` immediately updates the other.
6. Run `python3 tests/e2e/chat_scenario.py` for the deterministic three-client
   scenario.
7. Inspect persistence with:

```bash
docker compose exec -T mysql mysql -uchatroom -p chatroom -e \
  "SELECT COUNT(*) rooms FROM rooms; SELECT event_type,COUNT(*) FROM membership_events GROUP BY event_type; SELECT COUNT(*) messages FROM messages;"
```
