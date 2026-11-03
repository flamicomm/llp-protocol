# Design decisions — LLP v4.0.0

Architecture Decision Records for the v4 wire format and library. Each record
states the context, the decision, the consequences we accept, and what was
rejected (and why). The normative result lives in [`SPEC-v4.md`](SPEC-v4.md).

Format: one record per decision, newest last. A record is **immutable** once
accepted; superseding it means writing a new one.

---

## ADR-001 — Fixed 6-byte header

**Context.** v3.1.0 carries `MAGIC(2) + LEN(2)` and puts a layer chain (with
per-layer metadata) inside the payload. Every consumer we have (grbl-llp, the
ESP bridge, Java, Python) uses exactly one layer: FinalNode. The chain costs
one byte per frame plus a metadata parser and a chain walker in every
implementation.

**Decision.** v4 uses a fixed header: `MAGIC(2) + LEN(1) + CHANNEL(1)`. The
payload is opaque bytes. Minimum frame size drops from 7 to 6 bytes.

**Consequences.**
- One byte less per frame than v3, and a whole subsystem (metadata parsing)
  disappears from the parser, the encoder and every port.
- Multiplexing is preserved with `CHANNEL` (see ADR-005).
- A future transform layer (compression, encryption) would need a new wire
  version rather than being negotiated inside the frame.

**Rejected.** Keeping the chain for “future extensibility”: it was never used,
and the extension point that matters (multiple streams) is covered by 256
channels. Extension by version bump is explicit and cheap.

---

## ADR-002 — Keep `0xAA` escaping instead of COBS

**Context.** COBS guarantees a delimiter (`0x00`) never appears in the encoded
payload and has bounded overhead (`n + n/254 + 1`). LLP escapes `0xAA` as
`0xAA 0x00`, with a worst case of `2n`. For a 2 KB MCU, the 2× worst case looks
expensive.

**Decision.** Keep the `0xAA` escape and the `AA 55` magic.

**Consequences.**
- Worst-case wire size is `10 + 2n`; with a streaming encoder (ADR-008) this
  costs **wire time only**, never RAM.
- Resync is fast and local: the parser finds the next frame at the first
  `AA 55`, no need to wait for a delimiter.
- Text payloads (the common case) contain no `0xAA` at all, so the average
  overhead is zero.
- The stuffing rule is already implemented and tested in three languages.

**Rejected.**
- *COBS*: bounded overhead, but requires an encoding pass over the whole payload
  (or a stateful encoder with the same complexity), loses the “magic gives an
  immediate resync” property, and would invalidate the existing test vectors,
  ESP firmware and Java/Python ports for a gain that only materialises on
  pathological payloads.
- *SLIP*: same class of escaping (`0xC0`), no advantage here.
- *HDLC/PPP*: bit stuffing — inappropriate for a byte-oriented API.

---

## ADR-003 — CRC16-CCITT-FALSE, fixed in the wire

**Context.** CRC16 costs 2 bytes per frame. For tiny payloads (2–8 bytes) a
CRC8 would be cheaper, and some links would prefer CRC32.

**Decision.** CRC16-CCITT-FALSE (poly `0x1021`, init `0xFFFF`, no reflection, no
final XOR) over `MAGIC + LEN + CHANNEL + PAYLOAD`, transmitted little-endian.
Not configurable.

**Consequences.**
- Residual error probability ≈ 2⁻¹⁶ per corrupted frame, which is what “never
  delivers a corrupt frame” needs in a noisy environment.
- Implementations may use bitwise, nibble-table or full-table CRC; the wire
  bytes are identical.
- Mixed configurations cannot happen by accident (a common source of
  “works on my board” bugs).

**Rejected.** Making CRC width a compile-time option: two devices with different
settings would silently fail to interoperate, and there is no way to negotiate
it in a 6-byte header.

---

## ADR-004 — One-byte length, maximum payload 255

**Context.** v3 used a 2-byte little-endian length supporting 512+ byte
payloads. Real payloads are lines of text (≤ 64 bytes) or small telemetry
records.

**Decision.** `LEN` is 1 byte; payloads are 0–255 bytes. The parser validates
`LEN` against the instance capacity as soon as it is read.

**Consequences.**
- One byte less per frame and one byte less of state.
- Applications needing larger messages fragment at the application layer (or use
  a future version).
- A parser can be built for a 16-byte capacity and still be conformant: `LEN`
  greater than capacity yields `LLP_ERR_LEN`.

**Rejected.** A length-extension escape (like the metadata `0xFF` prefix in v3):
it reintroduces a variable header and the associated parsing edge cases for a
case nobody has.

---

## ADR-005 — `CHANNEL` byte replaces the layer chain

**Context.** v3 multiplexed with layer IDs plus per-layer metadata
(`[LAYER_ID][META_LEN][META...]...[0x00][RAW]`), supporting passthrough and
transform layers.

**Decision.** A single `CHANNEL` byte, opaque to the protocol, always present.

**Consequences.**
- Routing information costs 1 byte and is fixed-size, so the parser needs no
  chain walker (less flash, no malformed-chain error class).
- Receivers that do not care about channels use channel 0 and ignore the field.
- Reserved ranges leave room for future conventions (ADR-006).

**Rejected.** Keeping the chain “in case”: it is the single largest source of
code and error codes in the v3 parser (`LLP_ERR_MALFORMED_LAYER`,
`LLP_ERR_TRANSFORM_LAYER`) for a feature with no consumer.

---

## ADR-006 — No sequencing or ARQ in the base protocol

**Context.** In a noisy link, a frame can be lost or duplicated. Some links
(TCP) already guarantee delivery; raw UART does not.

**Decision.** Sequence numbers, acknowledgements and retransmission are **not**
in the header. They are an application-layer concern (or a future channel
convention).

**Consequences.**
- The base stays 6 bytes of overhead and provably stateless.
- A link that needs ARQ pays for it only there: one byte of sequence plus a
  small retransmission table in the application.
- Corrupted frames are already handled by the CRC; only whole-frame loss needs
  ARQ.

**Rejected.** A 1-byte sequence in every frame: it costs 17% of the overhead for
every user, including those over TCP, and does not by itself provide
retransmission.

---

## ADR-007 — Inter-byte timeout with a 16-bit wrap-safe timestamp

**Context.** A truncated frame must not block the parser forever, and reading a
clock inside the library would tie it to an OS/HAL.

**Decision.** The caller passes `now_ms` on every byte; the parser stores a
16-bit timestamp and compares with modular arithmetic:

```c
if ((uint16_t)(now_ms - last_byte_time) > timeout_ms) { /* timeout */ }
```

Default 2000 ms, configurable per instance, maximum 32767 ms. Strictly greater
than; no timeout is reported while waiting for the magic.

**Consequences.**
- 2 bytes of state instead of 4, no clock dependency, works from an ISR.
- Callers with a 32-bit millisecond counter simply truncate it (the arithmetic
  works as long as one timeout fits in 16 bits).
- Timeouts longer than 32767 ms are not representable (documented).

**Rejected.** Storing a 32-bit timestamp (+2 bytes per instance) to support
multi-minute timeouts.

---

## ADR-008 — Parser with caller-supplied payload buffer

**Context.** v3 embeds `uint8_t payload[LLP_MAX_PAYLOAD]` in the parser struct:
77 bytes at 64, 141 bytes at 128. On an ATmega328P with 2 KB of RAM that is a
significant share, and the typical consumer already has a line buffer where the
payload ends up.

**Decision.** The parser can be attached to an external buffer
(`llp_parser_attach(&p, buf, cap)`). The embedded-buffer mode remains available
for compatibility and for platforms where the simplicity matters more.

**Consequences.**
- Parser state drops to ~17 bytes in the low-RAM profile, and the payload is
  written directly into the consumer's buffer (usually a shared line buffer).
- The invariant “no side effects before CRC” is preserved: the consumer only
  *acts* on the payload when `feed()` reports a completed frame. On failure the
  consumer discards the partial buffer (a single length counter).
- The encoder mirrors this with a streaming writer (write callback), so no
  frame buffer is needed either: worst case `10 + 2n` bytes of wire, zero bytes
  of extra RAM.

**Rejected.** Requiring a fixed embedded buffer for simplicity: it forces every
consumer to pay 64–128 bytes regardless of its needs.

---

## ADR-009 — Breaking wire change, no dual-format mode

**Context.** v3.1.0 is deployed in grbl-llp, the ESP bridge, Java and Python,
but not in production (single machine, in testing).

**Decision.** v4 replaces v3 on the wire. The library does **not** support
parsing both formats. v3.1.0 remains available as tag `v3.1.0`; all consumers
migrate together (Phase 5 of the plan).

**Consequences.**
- The parser keeps one state machine and one error table: minimal flash.
- Migration is coordinated and verifiable with the conformance vectors.
- Anyone still on v3 must pin the tag; the changelog says so explicitly.

**Rejected.** A `version` nibble in the header: it costs a byte per frame and
doubles the parser’s job forever to support a transitional case that will last
weeks.

---

## ADR-010 — No multi-byte fields (endianness-free header)

**Context.** v3 had a 2-byte little-endian length; that is the only place where
byte order mattered, and it is a classic portability bug on big-endian MCUs.

**Decision.** All header fields are single bytes. The only multi-byte field is
the CRC, fixed little-endian.

**Consequences.**
- No endianness dependency anywhere in the parser or encoder except the final
  CRC pair (documented and covered by vectors).
- Reading a frame with a struct cast is still wrong (packing + stuffing); the
  API is byte-at-a-time.
