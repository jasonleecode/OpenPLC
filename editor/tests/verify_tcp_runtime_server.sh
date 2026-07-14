#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER="$SCRIPT_DIR/tools/tizi_tcp_runtime_server.py"
HOST="${TIZI_TCP_TEST_HOST:-127.0.0.1}"
PORT="${TIZI_TCP_TEST_PORT:-16699}"
LOG_FILE="$(mktemp -t tizi-tcp-runtime.XXXXXX.log)"

cleanup() {
  if [[ -n "${SERVER_PID:-}" ]]; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
  rm -f "$LOG_FILE"
}
trap cleanup EXIT

python3 "$SERVER" --host "$HOST" --port "$PORT" >"$LOG_FILE" 2>&1 &
SERVER_PID=$!

for _ in {1..50}; do
  if python3 - "$HOST" "$PORT" <<'PY' >/dev/null 2>&1
import socket
import sys

with socket.create_connection((sys.argv[1], int(sys.argv[2])), timeout=0.1):
    pass
PY
  then
    break
  fi
  if ! kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    cat "$LOG_FILE" >&2
    exit 1
  fi
  sleep 0.1
done

python3 - "$HOST" "$PORT" <<'PY'
import socket
import struct
import sys

SOF = 0xAA
ACK = b"\x06"
CMD_PING = 0x01
CMD_ERASE = 0x02
CMD_WRITE_PAGE = 0x03
CMD_VERIFY = 0x04
CMD_RESET = 0x05
CMD_GET_STATUS = 0x10
CMD_SET_RUN = 0x11
CMD_READ_IO = 0x12
USER_FLASH_BASE = 0x00004000
FLASH_PAGE_SIZE = 256


def crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x31) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def frame(cmd: int, payload: bytes = b"") -> bytes:
    return bytes([SOF, cmd, len(payload) & 0xFF, len(payload) >> 8]) + payload + bytes([crc8(payload)])


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise AssertionError("connection closed")
        data += chunk
    return data


def expect_ack(sock: socket.socket, cmd: int, payload: bytes = b"") -> None:
    sock.sendall(frame(cmd, payload))
    reply = recv_exact(sock, 1)
    if reply != ACK:
        raise AssertionError(f"command 0x{cmd:02x} returned {reply!r}, expected ACK")


def request_frame(sock: socket.socket, cmd: int, payload: bytes = b"") -> tuple[int, bytes]:
    sock.sendall(frame(cmd, payload))
    header = recv_exact(sock, 4)
    if header[0] != SOF:
        raise AssertionError(f"invalid SOF {header[0]:02x}")
    reply_cmd = header[1]
    size = header[2] | (header[3] << 8)
    payload_crc = recv_exact(sock, size + 1)
    reply_payload = payload_crc[:-1]
    if crc8(reply_payload) != payload_crc[-1]:
        raise AssertionError("invalid response CRC")
    return reply_cmd, reply_payload


host = sys.argv[1]
port = int(sys.argv[2])
page = bytes((index * 17 + 3) & 0xFF for index in range(FLASH_PAGE_SIZE))

with socket.create_connection((host, port), timeout=2.0) as sock:
    cmd, payload = request_frame(sock, CMD_PING)
    assert cmd == CMD_PING
    assert payload == b"TiZiv1.0"

    expect_ack(sock, CMD_ERASE)
    expect_ack(sock, CMD_WRITE_PAGE, struct.pack("<I", USER_FLASH_BASE) + page)
    expect_ack(sock, CMD_VERIFY, struct.pack("<IHB", USER_FLASH_BASE, len(page), crc8(page)))

    cmd, payload = request_frame(sock, CMD_GET_STATUS)
    assert cmd == CMD_GET_STATUS
    assert len(payload) == 5
    running, scan_time = struct.unpack("<BI", payload)
    assert running == 0
    assert scan_time == 0

    expect_ack(sock, CMD_SET_RUN, b"\x01")
    cmd, payload = request_frame(sock, CMD_GET_STATUS)
    running, _ = struct.unpack("<BI", payload)
    assert running == 1

    cmd, payload = request_frame(sock, CMD_READ_IO)
    assert cmd == CMD_READ_IO
    assert len(payload) == 2

    expect_ack(sock, CMD_RESET)

print("TiZi TCP runtime protocol test passed")
PY
