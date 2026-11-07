#!/usr/bin/env python3
"""LLP wire format 4.0.0 — Profile R1: reliable command channel.

Reference implementation of the optional profile defined in
``docs/RELIABILITY.md``:

    COMMAND (channel 1):  [SEQ][DATA...]      sender -> receiver
    ACK     (channel 2):  [SEQ]               receiver -> sender
    HELLO   (channel 3):  [SESSION]           sender -> receiver, resets dedup

This module implements **only the two state machines and the payload helpers**.
Framing stays in ``llp_v4.py``; serial/TCP I/O stays in the caller.

    Sender     stop-and-wait window of one, byte-identical retransmission,
               exhaustion reporting (never claims delivery)
    Receiver   duplicate suppression: a lost ACK can never run a command twice

Time is injected as an integer number of milliseconds and compared with 16-bit
wrap semantics, exactly like the C implementation in ``llp-protocol`` and the
firmware, so all three behave the same around the clock wrap.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, List

# ---------------------------------------------------------------------------
# Profile constants (normative, see docs/RELIABILITY.md §2)
# ---------------------------------------------------------------------------

CH_COMMAND = 1
CH_ACK = 2
CH_HELLO = 3

MAX_DATA = 63                 # DATA bytes per command frame
MAX_PAYLOAD = 1 + MAX_DATA    # SEQ + DATA

DEFAULT_ACK_TIMEOUT_MS = 50
DEFAULT_MAX_RETRIES = 5
DEFAULT_BACKOFF_MS = 1000

# Sender service results.
TX_IDLE = "idle"
TX_SEND = "send"
TX_EXHAUSTED = "exhausted"

# Default session marker carried by HELLO (opaque to the receiver).
DEFAULT_SESSION = 0x52


# ---------------------------------------------------------------------------
# Payload helpers
# ---------------------------------------------------------------------------

def command_payload(seq: int, data: bytes) -> bytes:
    """``[SEQ][DATA]`` for channel 1. Raises on an oversized payload."""
    if len(data) > MAX_DATA:
        raise ValueError(f"data too long for one frame: {len(data)} > {MAX_DATA}")
    return bytes([seq & 0xFF]) + bytes(data)


def ack_payload(seq: int) -> bytes:
    """``[SEQ]`` for channel 2."""
    return bytes([seq & 0xFF])


def hello_payload(session: int = DEFAULT_SESSION) -> bytes:
    """``[SESSION]`` for channel 3."""
    return bytes([session & 0xFF])


def chunks(data: bytes, size: int = MAX_DATA) -> Iterable[bytes]:
    """Split a message into consecutive DATA pieces (profile §5.3)."""
    if size < 1 or size > MAX_DATA:
        raise ValueError("chunk size out of range")
    for offset in range(0, len(data), size):
        yield data[offset:offset + size]


# ---------------------------------------------------------------------------
# Sender
# ---------------------------------------------------------------------------

def _elapsed(now_ms: int, then_ms: int) -> int:
    """Wrap-safe 16-bit elapsed milliseconds."""
    return (now_ms - then_ms) & 0xFFFF


@dataclass
class Sender:
    """Stop-and-wait sender with byte-identical retransmission."""

    timeout_ms: int = DEFAULT_ACK_TIMEOUT_MS
    max_retries: int = DEFAULT_MAX_RETRIES
    backoff_ms: int = DEFAULT_BACKOFF_MS

    active: bool = False
    exhausted: bool = False
    seq: int = 0
    payload: bytes = b""
    retries: int = 0
    sent_ms: int = 0

    def __post_init__(self) -> None:
        if self.timeout_ms <= 0:
            self.timeout_ms = DEFAULT_ACK_TIMEOUT_MS
        if self.backoff_ms <= 0:
            self.backoff_ms = DEFAULT_BACKOFF_MS

    # -- window ---------------------------------------------------------
    def start(self, seq: int, data: bytes, now_ms: int) -> bool:
        """Load a command into the window. Returns False when busy/oversized."""
        if self.active or len(data) > MAX_DATA:
            return False
        self.payload = command_payload(seq, data)
        self.seq = seq & 0xFF
        self.retries = 0
        self.exhausted = False
        self.sent_ms = now_ms
        self.active = True
        return True

    def service(self, now_ms: int) -> str:
        """Drive the timer: TX_IDLE, TX_SEND (retransmit) or TX_EXHAUSTED."""
        if not self.active:
            return TX_IDLE

        if self.exhausted:
            if _elapsed(now_ms, self.sent_ms) < self.backoff_ms:
                return TX_IDLE
            self.sent_ms = now_ms
            return TX_EXHAUSTED

        if self.retries >= self.max_retries:
            if _elapsed(now_ms, self.sent_ms) < self.timeout_ms:
                return TX_IDLE
            self.exhausted = True
            self.sent_ms = now_ms
            return TX_EXHAUSTED

        if _elapsed(now_ms, self.sent_ms) < self.timeout_ms:
            return TX_IDLE

        self.retries += 1
        self.sent_ms = now_ms
        return TX_SEND

    def on_ack(self, seq: int) -> bool:
        """Returns True when it acknowledged the outstanding frame."""
        if not self.active or (seq & 0xFF) != self.seq:
            return False
        self.active = False
        self.exhausted = False
        return True

    def abort(self) -> None:
        self.active = False
        self.exhausted = False


# ---------------------------------------------------------------------------
# Receiver
# ---------------------------------------------------------------------------

@dataclass
class Receiver:
    """Duplicate suppression: one ``last_seq`` is enough with window one."""

    last_seq: int = 0
    have_seq: bool = False
    delivered: int = 0
    duplicates: int = 0

    def accept(self, seq: int) -> bool:
        """True when DATA must be delivered, False for a retransmission.

        The caller MUST check that it can accept the whole DATA *before* calling
        this (profile §6.1): if it cannot, it must not consume the sequence
        number and must not ACK, so the sender retries later.
        """
        seq &= 0xFF
        if self.have_seq and seq == self.last_seq:
            self.duplicates += 1
            return False
        self.last_seq = seq
        self.have_seq = True
        self.delivered += 1
        return True

    def reset(self) -> None:
        """New session (HELLO): forget the last sequence number."""
        self.last_seq = 0
        self.have_seq = False
        self.delivered = 0
        self.duplicates = 0


# ---------------------------------------------------------------------------
# Manual CLI
# ---------------------------------------------------------------------------

def _main() -> int:
    sender = Sender()
    receiver = Receiver()

    sender.start(7, b"G1 X0\n", 0)
    print("command payload:", sender.payload.hex())
    print("service @0   :", sender.service(0))
    print("service @50  :", sender.service(50))
    receiver.accept(7)
    print("duplicate    :", receiver.accept(7))
    print("ack          :", sender.on_ack(7))
    print("ack payload  :", ack_payload(7).hex())
    print("hello payload:", hello_payload().hex())
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
