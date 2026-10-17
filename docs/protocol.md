# Chat room wire protocol

The transport is a stream of frames. Each frame starts with a four-byte unsigned
big-endian body length. The length excludes the header and must be in the range
1..65,536. The body is UTF-8 JSON. A zero/oversized length, malformed JSON, or an
invalid UTF-8 string is a fatal protocol error and the server closes the
connection. A valid business request that cannot be completed receives
`response.error`; the connection remains open.

Every client command is an object containing a string `type`, a 1..64 byte
string `request_id`, and an object `payload`:

| Command | Payload | Meaning |
|---|---|---|
| `session.hello` | `{"nickname":"alice"}` | Register a unique online nickname (1..32 UTF-8 bytes). |
| `room.create` | `{"name":"C++"}` | Persist a room and creator JOIN, then join it (name: 1..64 bytes). |
| `room.join` | `{"room_id":1}` | Persist JOIN and then join an existing room. |
| `room.leave` | `{}` | Persist LEAVE and then leave the current room. |
| `chat.send` | `{"text":"hello"}` | Persist and broadcast 1..4,096 UTF-8 bytes. |
| `ping` | `{"client_time":123}` | Echo the optional client timestamp in `pong`. |

The server emits these seven message types:

- `response.ok`: request completed and contains the original `request_id`.
- `response.error`: contains the original `request_id` and an `error` object
  with stable `code` and human-readable `message` fields.
- `room.snapshot`: request-correlated room id, name, and complete member list.
- `member.joined`: room id and nickname of a newly joined member.
- `member.left`: room id and nickname of a departed member.
- `chat.message`: room id, generated message id, sender, and plain-text body.
- `pong`: original request id and echoed `client_time` value.

Error codes are `INVALID_MESSAGE`, `UNSUPPORTED_TYPE`, `NICKNAME_TAKEN`,
`SESSION_REQUIRED`, `ALREADY_IN_ROOM`, `ROOM_NOT_FOUND`, `NOT_IN_ROOM`,
`MESSAGE_TOO_LARGE`, `SERVER_BUSY`, `DB_BUSY`, and `DB_ERROR`.

One connection may have one online session and may belong to at most one room.
Explicit create/join/leave/chat changes become visible only after persistence
succeeds. Disconnect cleanup is memory-first; recording its LEAVE event is best
effort.
