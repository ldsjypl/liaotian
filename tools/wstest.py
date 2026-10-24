#!/usr/bin/env python3
"""Minimal WebSocket client (no deps) used to smoke-test the wss path."""
import base64
import json
import os
import socket
import ssl
import struct
import sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 443
SNI = sys.argv[3] if len(sys.argv) > 3 else "ldsjypl.online"
PATH = sys.argv[4] if len(sys.argv) > 4 else "/ws?k=735f28"


def handshake():
    ctx = ssl._create_unverified_context()
    raw = socket.create_connection((HOST, PORT), timeout=10)
    sock = ctx.wrap_socket(raw, server_hostname=SNI)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        f"GET {PATH} HTTP/1.1\r\n"
        f"Host: {SNI}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("closed during handshake")
        buf += chunk
    head, _, rest = buf.partition(b"\r\n\r\n")
    status = head.split(b"\r\n")[0].decode()
    if "101" not in status:
        raise RuntimeError("handshake failed: " + status)
    return sock, rest


def send_text(sock, obj):
    payload = json.dumps(obj, ensure_ascii=False).encode()
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    n = len(payload)
    if n < 126:
        head = struct.pack("!BB", 0x81, 0x80 | n)
    else:
        head = struct.pack("!BBH", 0x81, 0x80 | 126, n)
    sock.sendall(head + mask + masked)


class Reader:
    def __init__(self, sock, rest=b""):
        self.sock = sock
        self.buf = rest

    def _need(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("socket closed")
            self.buf += chunk

    def frame(self):
        self._need(2)
        b0, b1 = self.buf[0], self.buf[1]
        op = b0 & 0x0F
        ln = b1 & 0x7F
        off = 2
        if ln == 126:
            self._need(4)
            ln = struct.unpack("!H", self.buf[2:4])[0]
            off = 4
        elif ln == 127:
            self._need(10)
            ln = struct.unpack("!Q", self.buf[2:10])[0]
            off = 10
        self._need(off + ln)
        payload = self.buf[off:off + ln]
        self.buf = self.buf[off + ln:]
        return op, payload


def main():
    sock, rest = handshake()
    rd = Reader(sock, rest)
    print("handshake ok")

    send_text(sock, {"type": "join", "name": "tester"})
    op, payload = rd.frame()
    msg = json.loads(payload)
    print("welcome:", msg["type"], "name=", msg["name"], "users=", msg["users"],
          "history=", len(msg.get("history", [])))

    send_text(sock, {"type": "msg", "text": "hello over wss"})
    op, payload = rd.frame()
    msg = json.loads(payload)
    print("echo:", msg["type"], msg["from"], "->", msg["text"])

    send_text(sock, {"type": "msg", "text": "第二条：中文与 emoji 🎉"})
    op, payload = rd.frame()
    msg = json.loads(payload)
    print("echo2:", msg["from"], "->", msg["text"])

    sock.close()
    print("done")


if __name__ == "__main__":
    main()
