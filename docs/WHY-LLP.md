# Why LLP? (and when not to use it)

LLP is a **framing layer** for byte-oriented links that can corrupt, drop or
invent bytes. This document justifies the design against the usual alternatives
so the choice can be reviewed instead of inherited.

The normative format is in [`SPEC-v4.md`](SPEC-v4.md); the decisions behind it
in [`DECISIONS.md`](DECISIONS.md).

## The problem it solves

A UART link next to a brushed spindle does not deliver neat lines of text. It
delivers: flipped bits, lost bytes, inserted bytes, and occasionally silence.
A protocol without integrity checking turns any of those into a *silently wrong
command* — which on a CNC means a wrong move, not an error message.

LLP’s single hard guarantee is:

> **A frame reaches the application only if every byte of it survived intact.**

Everything else (channels, timeouts, resync) exists to keep that guarantee cheap
on a 2 KB microcontroller.

## Comparison

| | Overhead (min) | Overhead (worst) | Integrity | Resync | Multiplexing | Deterministic | Code/RAM |
|---|---:|---:|---|---|---|---|---|
| **LLP v4** | 6 B | 10 + 2n | CRC16 | magic `AA 55` (2 B) | 1-byte channel | yes (no timing, no gaps) | small: 7-state FSM, fixed work per byte |
| Plain text + `\n` | 0 | 0 | none | none | none | yes | trivial |
| SLIP | 2 | 2n | none (by spec) | delimiter `C0` | none | yes | small |
| COBS | ~1 | n + n/254 + 1 | none (usually + CRC) | delimiter `00` | none | yes | medium (encode/decode passes) |
| HDLC / PPP (async) | 2 | 2n | FCS16/32 | flag `7E` | protocol field | yes | larger |
| Modbus RTU | 4 (addr+CRC) | 4 | CRC16 | **3.5-char silent gap** | slave address (1) | **no** (timing-based) | small |

Notes:

- **Plain text** is what grbl speaks. It is perfect for a short, quiet USB cable
  and hopeless next to a spindle: a flipped bit in `X-10.5` becomes `X-90.5` and
  nothing notices.
- **SLIP** is minimal but deliberately has no integrity check; you must add a
  CRC, at which point you have re-invented LLP with a worse resync story (a lost
  `0xC0` merges two frames).
- **COBS** is the strongest alternative: bounded overhead and simple decoding.
  Its weakness for this use case is that the delimiter carries no information —
  a bit error that turns a data byte into `0x00` splits a frame, and the
  receiver must wait for the *next* delimiter to resynchronise. LLP resynchronises
  on a 2-byte magic that essentially never occurs by chance in text
  (`0xAA` is not an ASCII byte).
- **HDLC/PPP** is the industrial answer, with a richer header and more code than
  a 2 KB MCU wants.
- **Modbus RTU** frames by *silence*. That works on a bare-metal UART with tight
  timing, but breaks under an RTOS, over USB bridges (which batch bytes), or in
  the presence of noise that inserts gaps.

## Why the magic is `0xAA 0x55`

- `0xAA` is not printable in ASCII or UTF-8, so text payloads never contain it
  by accident; the escape rule therefore costs nothing in the common case.
- Two bytes give a 1/65536 chance of a random pair matching, and the CRC then
  rejects the false frame.
- A false magic mid-frame is cheap to detect: it aborts and restarts, so the
  parser is never “stuck” more than one frame behind.

## Cost model

| Payload | Frame bytes (best) | Overhead |
|---:|---:|---:|
| 0 | 6 | — |
| 8 | 14 | 75% |
| 32 | 38 | 19% |
| 64 | 70 | 9% |
| 255 | 261 | 2.4% |

Guidance: keep payloads in the 16–64 byte range. Below ~8 bytes the header
dominates; above 255 the protocol needs fragmentation (out of scope).

## When **not** to use LLP

- **Bulk transfer** (firmware updates, file transfer, audio): 6 bytes per frame
  and a 2× worst case are the wrong trade. Use a stream protocol with a window
  (XMODEM, YMODEM, or a TCP-like window).
- **A link that already guarantees framing and integrity** (e.g. an internal
  TCP connection between two processes): LLP adds nothing except overhead.
  (LLP is still useful at the *serial* hop behind such a link — that is exactly
  what grbl-llp does.)
- **Peers that cannot run the parser** (a stock terminal, a third-party tool):
  use plain text and accept the risk, or put a translator in front.
- **Messages > 255 bytes** without an application-level fragmentation scheme.

## Non-goals

LLP does not encrypt, compress, order, retransmit or flow-control. Those are
layers above it. Keeping the base stateless is what makes it portable to a
7-state FSM with ~17 bytes of RAM in the low-resource profile.
