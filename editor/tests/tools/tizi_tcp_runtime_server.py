#!/usr/bin/env python3
"""Small TiZi TCP runtime server used by editor integration tests.

It implements the same byte-framed protocol used by runtime/app/runtime.c,
but stores the user flash region in memory. This is a desktop test double for
the future Ethernet runtime service.
"""

from __future__ import annotations

import argparse
import socket
import struct
from dataclasses import dataclass, field


SOF = 0xAA
ACK = bytes([0x06])
NAK = bytes([0x15])

CMD_PING = 0x01
CMD_ERASE = 0x02
CMD_WRITE_PAGE = 0x03
CMD_VERIFY = 0x04
CMD_RESET = 0x05
CMD_GET_STATUS = 0x10
CMD_SET_RUN = 0x11
CMD_READ_IO = 0x12

USER_FLASH_BASE = 0x00004000
USER_FLASH_SIZE = 16 * 1024
FLASH_PAGE_SIZE = 256


def crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x31) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def frame(cmd: int, payload: bytes) -> bytes:
    return bytes([SOF, cmd, len(payload) & 0xFF, len(payload) >> 8]) + payload + bytes([crc8(payload)])


@dataclass
class RuntimeState:
    flash: bytearray = field(default_factory=lambda: bytearray([0xFF] * USER_FLASH_SIZE))
    running: bool = False
    scan_time_us: int = 0
    do_state: int = 0

    def addr_to_offset(self, addr: int, size: int) -> int | None:
        if addr < USER_FLASH_BASE:
            return None
        offset = addr - USER_FLASH_BASE
        if offset + size > len(self.flash):
            return None
        return offset


def handle_command(state: RuntimeState, cmd: int, payload: bytes) -> bytes:
    if cmd == CMD_PING:
        return frame(CMD_PING, b"TiZiv1.0")

    if cmd == CMD_ERASE:
        state.flash[:] = b"\xFF" * len(state.flash)
        return ACK

    if cmd == CMD_WRITE_PAGE:
        if len(payload) != 4 + FLASH_PAGE_SIZE:
            return NAK
        addr = struct.unpack_from("<I", payload, 0)[0]
        offset = state.addr_to_offset(addr, FLASH_PAGE_SIZE)
        if offset is None:
            return NAK
        state.flash[offset:offset + FLASH_PAGE_SIZE] = payload[4:]
        return ACK

    if cmd == CMD_VERIFY:
        if len(payload) != 7:
            return NAK
        addr, size, expected = struct.unpack("<IHB", payload)
        offset = state.addr_to_offset(addr, size)
        if offset is None:
            return NAK
        return ACK if crc8(bytes(state.flash[offset:offset + size])) == expected else NAK

    if cmd == CMD_RESET:
        return ACK

    if cmd == CMD_GET_STATUS:
        return frame(CMD_GET_STATUS, struct.pack("<BI", int(state.running), state.scan_time_us))

    if cmd == CMD_SET_RUN:
        if len(payload) != 1:
            return NAK
        state.running = payload[0] != 0
        return ACK

    if cmd == CMD_READ_IO:
        return frame(CMD_READ_IO, bytes([0, state.do_state]))

    return NAK


def recv_exact(conn: socket.socket, size: int) -> bytes | None:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = conn.recv(remaining)
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def serve(host: str, port: int) -> None:
    state = RuntimeState()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(1)
        print(f"TiZi TCP runtime server listening on {host}:{port}", flush=True)
        while True:
            conn, _ = srv.accept()
            with conn:
                while True:
                    header = conn.recv(1)
                    if not header:
                        break
                    if header[0] != SOF:
                        continue
                    rest = recv_exact(conn, 3)
                    if rest is None:
                        break
                    cmd = rest[0]
                    size = rest[1] | (rest[2] << 8)
                    payload_crc = recv_exact(conn, size + 1)
                    if payload_crc is None:
                        break
                    payload = payload_crc[:-1]
                    actual_crc = payload_crc[-1]
                    if crc8(payload) != actual_crc:
                        conn.sendall(NAK)
                        continue
                    conn.sendall(handle_command(state, cmd, payload))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6699)
    args = parser.parse_args()
    serve(args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
