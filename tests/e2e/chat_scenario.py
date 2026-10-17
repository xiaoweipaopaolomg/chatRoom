#!/usr/bin/env python3
import argparse
import json
import socket
import struct
import time

MAX_BODY = 65536


class Client:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(5)
        self.buffer = bytearray()

    def send(self, message, split=False):
        body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
        frame = struct.pack("!I", len(body)) + body
        if split:
            self.sock.sendall(frame[:2])
            self.sock.sendall(frame[2:7])
            self.sock.sendall(frame[7:])
        else:
            self.sock.sendall(frame)

    def receive(self):
        while len(self.buffer) < 4:
            self.buffer.extend(self.sock.recv(4096))
        length = struct.unpack("!I", self.buffer[:4])[0]
        if not 0 < length <= MAX_BODY:
            raise AssertionError("invalid response frame length")
        while len(self.buffer) < 4 + length:
            data = self.sock.recv(4096)
            if not data:
                raise AssertionError("server disconnected")
            self.buffer.extend(data)
        body = bytes(self.buffer[4:4 + length])
        del self.buffer[:4 + length]
        return json.loads(body)

    def wait_type(self, wanted):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            message = self.receive()
            if message.get("type") == wanted:
                return message
        raise AssertionError("did not receive " + wanted)

    def close(self):
        self.sock.close()


def command(kind, request_id, payload):
    return {"type": kind, "request_id": request_id, "payload": payload}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7777)
    args = parser.parse_args()
    alice, bob, carol = [Client(args.host, args.port) for _ in range(3)]
    try:
        alice.send(command("session.hello", "a1", {"nickname": "alice"}), split=True)
        assert alice.receive()["type"] == "response.ok"
        alice.send(command("room.create", "a2", {"name": "C++"}))
        snapshot = alice.wait_type("room.snapshot")
        room_id = snapshot["room"]["id"]
        for client, nickname in ((bob, "bob"), (carol, "carol")):
            client.send(command("session.hello", nickname + "1", {"nickname": nickname}))
            assert client.receive()["type"] == "response.ok"
            client.send(command("room.join", nickname + "2", {"room_id": room_id}))
            assert client.wait_type("room.snapshot")["room"]["id"] == room_id
            alice.wait_type("member.joined")
        alice.send(command("chat.send", "a3", {"text": "hello from e2e"}))
        for client in (alice, bob, carol):
            assert client.wait_type("chat.message")["text"] == "hello from e2e"
        carol.close()
        assert alice.wait_type("member.left")["nickname"] == "carol"
        assert bob.wait_type("member.left")["nickname"] == "carol"
        print(json.dumps({"status": "ok", "room_id": room_id}))
    finally:
        alice.close()
        bob.close()


if __name__ == "__main__":
    main()
