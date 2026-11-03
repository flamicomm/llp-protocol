#!/usr/bin/env python3
"""LLP wire format 4.0.0 — Python reference implementation.

Small and literal on purpose: this file is the executable form of
``docs/SPEC-v4.md`` and the cross-language reference used to generate and
self-check the conformance vectors (``scripts/gen_vectors_v4.py``).

Wire format 4.0.0:

    [0xAA][0x55][LEN][CHANNEL][PAYLOAD...][CRC_L][CRC_H]

  - LEN     : payload length, 0..255 (1 byte)
  - CHANNEL : application routing, 0..255 (1 byte)
  - PAYLOAD : raw application bytes, LEN bytes
  - CRC16   : CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection,
              no final XOR) over MAGIC + LEN + CHANNEL + PAYLOAD, little-endian
  - Stuffing: every 0xAA after the magic is sent as 0xAA 0x00

Time is caller-supplied and reduced modulo 65536 (the C parser stores a
16-bit timestamp). Timeouts are wrap-safe and must be <= 32767 ms.
"""

from __future__ import annotations

MAGIC = b"\xaa\x55"
MAGIC1 = 0xAA
MAGIC2 = 0x55
CRC_INIT = 0xFFFF
MAX_PAYLOAD = 255
DEFAULT_TIMEOUT_MS = 2000

# Parser states
WAIT_MAGIC1, WAIT_MAGIC2, READ_LEN, READ_CHANNEL, READ_PAYLOAD, READ_CRC_L, READ_CRC_H = range(7)

# Error codes (identical values in the C API)
ERR_CRC = "LLP_ERR_CRC"
ERR_LEN = "LLP_ERR_LEN"
ERR_TIMEOUT = "LLP_ERR_TIMEOUT"
ERR_SYNC = "LLP_ERR_SYNC"


# --------------------------------------------------------------------------
# CRC
# --------------------------------------------------------------------------
def crc16_update(crc: int, byte: int) -> int:
    """CRC16-CCITT bitwise update (poly 0x1021, init 0xFFFF)."""
    crc ^= (byte & 0xFF) << 8
    for _ in range(8):
        if crc & 0x8000:
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF
        else:
            crc = (crc << 1) & 0xFFFF
    return crc


def crc16(data: bytes, crc: int = CRC_INIT) -> int:
    for byte in data:
        crc = crc16_update(crc, byte)
    return crc


def _crc_seed() -> int:
    return crc16_update(crc16_update(CRC_INIT, MAGIC1), MAGIC2)


# --------------------------------------------------------------------------
# Encoder
# --------------------------------------------------------------------------
def encode(channel: int, payload: bytes) -> bytes:
    """Encode one frame (channel, payload) -> wire bytes."""
    if not 0 <= len(payload) <= MAX_PAYLOAD:
        raise ValueError("payload length out of range 0..255")
    if not 0 <= channel <= 0xFF:
        raise ValueError("channel out of range 0..255")

    body = bytes([len(payload), channel]) + payload
    crc = crc16(body, _crc_seed())

    frame = bytearray(MAGIC)
    for byte in body + bytes([crc & 0xFF, (crc >> 8) & 0xFF]):
        if byte == MAGIC1:
            frame.append(MAGIC1)
            frame.append(0x00)
        else:
            frame.append(byte)
    return bytes(frame)


def frame_size(payload_len: int) -> int:
    """Worst-case on-wire size for `payload_len` payload bytes (all stuffed)."""
    return 2 + 2 * (1 + 1 + payload_len) + 2 * 2


# --------------------------------------------------------------------------
# Decoder
# --------------------------------------------------------------------------
class Parser:
    """Byte-at-a-time parser. Events are returned in detection order."""

    def __init__(self, capacity: int = MAX_PAYLOAD, timeout_ms: int = DEFAULT_TIMEOUT_MS):
        if not 0 <= capacity <= MAX_PAYLOAD:
            raise ValueError("capacity out of range 0..255")
        self.capacity = capacity
        self.timeout_ms = timeout_ms
        self.reset()

    def reset(self) -> None:
        self.state = WAIT_MAGIC1
        self.escape_pending = 0
        self.length = 0
        self.channel = 0
        self.payload = bytearray()
        self.crc_calculated = _crc_seed()
        self.crc_received = 0
        self.last_byte_time = 0
        self.frames_ok = 0
        self.frames_error = 0
        self.timeouts = 0

    # -- internals -------------------------------------------------------
    def _start_frame(self) -> None:
        self.state = READ_LEN
        self.escape_pending = 0
        self.length = 0
        self.channel = 0
        self.payload = bytearray()
        self.crc_calculated = _crc_seed()

    def _fail(self, code: str, events: list) -> list:
        self.frames_error += 1
        events.append(("error", code))
        self.state = WAIT_MAGIC1
        self.escape_pending = 0
        self.payload = bytearray()
        return events

    # -- public ----------------------------------------------------------
    def feed(self, byte: int, now_ms: int) -> list[tuple]:
        """Feed one byte. Returns a list of events:

        ("frame", channel, payload) | ("error", CODE) | ("timeout",)
        """
        events: list[tuple] = []
        byte &= 0xFF

        if self.state != WAIT_MAGIC1:
            if (now_ms - self.last_byte_time) & 0xFFFF > self.timeout_ms:
                self.timeouts += 1
                self.frames_error += 1
                events.append(("error", ERR_TIMEOUT))
                self.state = WAIT_MAGIC1
                self.escape_pending = 0
                self.payload = bytearray()
                self.last_byte_time = now_ms & 0xFFFF
                if byte == MAGIC1:
                    self.state = WAIT_MAGIC2
                return events

        self.last_byte_time = now_ms & 0xFFFF

        if self.state not in (WAIT_MAGIC1, WAIT_MAGIC2):
            if self.escape_pending:
                self.escape_pending = 0
                if byte == MAGIC2:
                    # Unescaped magic inside a frame: resync.
                    self.frames_error += 1
                    events.append(("error", ERR_SYNC))
                    self._start_frame()
                    return events
                if byte == 0x00:
                    byte = MAGIC1
                else:
                    return self._fail(ERR_SYNC, events)
            elif byte == MAGIC1:
                self.escape_pending = 1
                return events

        if self.state == WAIT_MAGIC1:
            if byte == MAGIC1:
                self.state = WAIT_MAGIC2

        elif self.state == WAIT_MAGIC2:
            if byte == MAGIC2:
                self._start_frame()
            elif byte != MAGIC1:
                self.state = WAIT_MAGIC1

        elif self.state == READ_LEN:
            if byte > self.capacity:
                return self._fail(ERR_LEN, events)
            self.length = byte
            self.crc_calculated = crc16_update(self.crc_calculated, byte)
            self.state = READ_CHANNEL

        elif self.state == READ_CHANNEL:
            self.channel = byte
            self.crc_calculated = crc16_update(self.crc_calculated, byte)
            self.state = READ_PAYLOAD if self.length else READ_CRC_L

        elif self.state == READ_PAYLOAD:
            self.payload.append(byte)
            self.crc_calculated = crc16_update(self.crc_calculated, byte)
            if len(self.payload) == self.length:
                self.state = READ_CRC_L

        elif self.state == READ_CRC_L:
            self.crc_received = byte
            self.state = READ_CRC_H

        elif self.state == READ_CRC_H:
            self.crc_received |= byte << 8
            if self.crc_received != self.crc_calculated:
                return self._fail(ERR_CRC, events)
            self.frames_ok += 1
            events.append(("frame", self.channel, bytes(self.payload)))
            self.state = WAIT_MAGIC1
            self.payload = bytearray()

        return events


# --------------------------------------------------------------------------
# Tiny CLI for manual checks: echo "aa55..." | python3 llp_v4.py
# --------------------------------------------------------------------------
def _main() -> int:
    import sys

    if len(sys.argv) > 1 and sys.argv[1] == "encode":
        channel = int(sys.argv[2]) if len(sys.argv) > 2 else 0
        payload = sys.argv[3].encode() if len(sys.argv) > 3 else b""
        print(encode(channel, payload).hex())
        return 0

    data = bytes.fromhex(sys.stdin.read().strip())
    parser = Parser()
    for index, byte in enumerate(data):
        for event in parser.feed(byte, index):
            print(event)
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
