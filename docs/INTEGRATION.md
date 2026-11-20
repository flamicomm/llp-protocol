# Integrating LLP v4

LLP is a framing layer: it turns a **byte stream** into **validated messages**.
It never touches hardware. This guide is the whole contract: three primitives,
two invariants, a handful of pitfalls.

Reference adapters: [`examples/v4/`](../examples/v4/) (AVR bare-metal with
interrupt rings, POSIX file descriptors/TCP).

## The three primitives

An integration provides these, and nothing else:

| Primitive | Direction | Contract |
|---|---|---|
| `feed(byte)` | RX | Hand each received byte to `llp4_parser_feed()` |
| `sink(byte)` | TX | Write one byte to the link (UART, socket, radio) |
| `now_ms()` | time | Current time, truncated to `uint16_t` |

The library supplies the parser (`llp4_parser_*`), the streaming encoder
(`llp4_tx_*`) and the convenience wrapper (`llp4_encode`).

## Recipe

### 1. Storage

```c
#include "llp_v4.h"

static uint8_t payload[64];          /* parser payload buffer (your line buffer!) */
static llp4_parser_t parser;
```

The parser writes the payload **directly** into your buffer — pick the buffer
you already use for the received data. Its capacity is the maximum frame you
accept; a larger `LEN` is rejected with `LLP4_ERR_LEN` before any byte is stored.

### 2. Receive

```c
void on_byte_received(uint8_t byte, uint16_t now_ms)
{
    if (llp4_parser_feed(&parser, byte, now_ms) == LLP4_EV_FRAME) {
        uint8_t len;
        const uint8_t *data = llp4_parser_payload(&parser, &len);
        handle(llp4_parser_channel(&parser), data, len);
    }
}
```

`feed()` returns `1` for a frame, `0` for progress, or a negative error code
(`p.error` holds it). **Errors are events, not states**: after reporting one the
parser is already listening for the next frame.

### 3. Transmit

```c
static void sink(void *ctx, uint8_t byte) { link_write(byte); }

void send_frame(uint8_t channel, const uint8_t *data, uint8_t len)
{
    llp4_tx_t tx;
    llp4_tx_begin(&tx, channel, len, sink, NULL);
    for (uint8_t i = 0; i < len; i++) {
        llp4_tx_byte(&tx, data[i]);
    }
    llp4_tx_end(&tx);
}
```

The encoder streams: it needs no frame buffer, only the payload length up front
(it goes in the header). If you write into a ring, **reserve the worst case
first** — `llp4_frame_size(len)` is `10 + 2*len`:

```c
if (llp4_ring_free(&tx_ring) < llp4_frame_size(len)) {
    /* wait (bounded) or drop the whole frame; never write half a frame */
}
```

A half-written frame is not dangerous (the receiver resynchronises), but it
wastes bandwidth and looks like noise. All-or-nothing is cheaper to reason
about.

### 4. Time

Pass a 16-bit millisecond counter. Wrap-around is handled:

```c
uint16_t now = (uint16_t)millis();
```

Timeouts must be `<= 32767 ms`, the largest gap the wrap-safe comparison can
represent (SPEC-v4 §8). `llp4_parser_init()` enforces it: a timeout of `0`
selects `LLP4_DEFAULT_TIMEOUT_MS` and anything above 32767 is clamped, so a
misconfigured value cannot turn into "every frame times out" or "the timeout
never fires". No timeout is reported while waiting for the magic, so an idle
link is normal.

On AVR the same 16-bit counter is written by the timer ISR and read by the main
loop, so a plain `uint16_t now = ticks;` can tear: the ISR may fire between the
two byte loads. Take a consistent snapshot with interrupts disabled and restore
the previous interrupt state (an unconditional `sei()` would enable interrupts
from inside another critical section):

```c
static volatile uint16_t ticks;

static uint16_t ticks_read(void)
{
    uint8_t sreg = SREG;
    uint16_t value;

    cli();
    value = ticks;
    SREG = sreg;
    return value;
}
```

`ATOMIC_BLOCK(ATOMIC_RESTORESTATE)` from `<util/atomic.h>` is the same pattern
with a macro. Only the ISR writes the counter, so a single snapshot is enough;
see `examples/v4/avr_baremetal/main.c`.

### 5. Channels

`CHANNEL` is yours. A convention that works well:

| Channel | Content |
|---:|---|
| 0 | text lines (`"G1 X10\n"`, `"PING\n"`) |
| 1 | binary commands |
| 2 | telemetry |
| 3 | logs |

Text protocols keep working exactly as before: assemble payload bytes into a
line buffer, split on `\n`, and treat complete lines as commands (see
`examples/v4/avr_baremetal/main.c`).

## Context rules (important)

- **One parser per link**, fed from **one context**. If the UART interrupt calls
  `feed()`, the main loop must not. (Distinct parsers are independent.)
- The parser and encoder are safe to call from an ISR: they only touch their own
  state, never the hardware and never a clock.
- The **sink must be ISR-safe** if you encode from an ISR. Simplest model: encode
  from the main loop and let the ISR only drain the ring.
- Never mix: if `feed()` runs in an ISR and the frame handler does slow work,
  queue the event (set a flag, push to a ring) and handle it in the main loop.

## Sizing

| Item | Rule of thumb |
|---|---|
| RX ring | ≥ 2× the largest frame, so an ISR burst is not dropped |
| TX ring | ≥ 2× the worst-case frame (`10 + 2n`), so a frame always fits |
| Payload buffer | as large as the longest payload you accept |

On a 2 KB AVR a 64-byte RX ring, a 96-byte TX ring and a 64-byte payload buffer
are comfortable (≈ 220 bytes of RAM total).

## RS-485 half duplex

The only extra step is the transceiver direction pin: assert it **before** the
first byte of a frame and release it **after** the last byte has left the UART
(the ring is empty *and* the shift register is done, i.e. `TXC` flag).

```c
llp4_tx_begin(&tx, ch, len, sink_de, ctx);   /* drive DE high in sink() lazily */
...
llp4_tx_end(&tx);
link_drain();       /* wait for TXC, then release DE */
```

Do not release DE when the ring is empty: the last byte may still be shifting.
Polling `UCSR0A & (1 << TXC0)` (and clearing it) is enough on AVR.

## Text-over-LLP (grbl-style command protocols)

If the other end speaks lines, keep the line protocol above LLP:

```
LLP frame (channel 0, payload "G1 X10\n")  ->  line buffer  ->  command parser
```

This is what `grbl-llp` does: LLP only guarantees the bytes, the command parser
is unchanged. The line buffer can be the same buffer the LLP parser writes into.

## Reliable command channel (Profile R1, optional)

A lost command frame stalls a sender that waits for an application answer, and
blind retransmission can execute a command twice. Profile R1 adds a sequence
number, an ACK and duplicate suppression on top of two channels:

```
COMMAND (channel 1):  [SEQ][DATA...]
ACK     (channel 2):  [SEQ]
```

The normative text is in
[llp-spec `docs/RELIABILITY.md`](https://github.com/flamicomm/llp-spec/blob/main/docs/RELIABILITY.md);
the C state machines are in
[`llp_v4_rel.h`](../include/llp_v4_rel.h) and
[`RELIABILITY.md`](RELIABILITY.md) explains how to wire them.

The rule that is easy to get wrong: **check that the payload fits before
consuming the sequence number**. If it does not fit, do not ACK, so the sender
retries instead of the command being lost.

## Threat model

LLP is a framing layer for a link you already trust; it is not a security
protocol. Two properties are easy to over-read, so state them plainly:

| Claim | What it actually means |
|---|---|
| The CRC16 catches corruption | It is an **integrity check against noise**: bit flips, dropped or inserted bytes, a truncated frame. It is not a MAC: there is no key, an adversary who can write to the link can compute a valid frame byte by byte, and any 16-bit residue collides with probability 2⁻¹⁶ |
| Profile R1 suppresses duplicates | It remembers the **last accepted `SEQ`** on the receiver, so an immediate retransmission (lost ACK) is not delivered twice. It is **not anti-replay**: any frame carrying a different `SEQ` is accepted, a recorded frame replayed much later is accepted, and `HELLO` on channel 3 — which resets the session on purpose, so a rebooted sender is not mistaken for a duplicate — is itself unauthenticated |

What follows from that:

- **No confidentiality.** Payloads are plaintext on the wire; stuffing is an
  escape mechanism, not encryption.
- **No authentication, no identity.** `CHANNEL` is routing, not a capability.
  Channel 2 (ACK) and channel 3 (HELLO) are consumed by the ESP bridge; whoever
  can send bytes can also ACK, forge a session reset or inject commands.
- **No delivery guarantee.** R1 retransmits a fixed number of times and then
  reports exhaustion: the caller decides what an undelivered command means.
- **No flow control and no ordering across channels.** Channel 0 is fire and
  forget by design.
- **Availability is not defended.** A link that is jammed or flooded just makes
  the parser emit errors and the sender exhaust retries; the recovery behaviour
  measured in `docs/ROBUSTNESS.md` assumes noise, not an attacker.

The trust boundary is therefore the physical or link layer: a UART/RS-485 cable
inside the machine, or a TCP link that is already authenticated elsewhere. If the
link crosses a network you do not control, terminate an authenticated, encrypted
channel (TLS, WireGuard, an SSH tunnel) underneath and keep LLP as framing only —
do not add a bespoke MAC to the payload and call it security.

## Testing your integration

1. Run the conformance vectors first (`docs/CONFORMANCE.md`) — they validate the
   framing independently of your hardware.
2. Use `examples/v4/host_posix/host_posix.c --selftest` as a template for a
   loopback test with noise injection.
3. On the target, send a few frames with a known payload and confirm the CRC
   counter (`frames_ok`) increments and no `frames_error` appears.

## Pitfalls checklist

- [ ] Payload buffer capacity ≥ the longest payload (`LEN` is checked early).
- [ ] TX ring can hold `10 + 2n` before encoding starts.
- [ ] Time is 16-bit, wrap-safe, timeout ≤ 32767 ms.
- [ ] The transport feeds real elapsed milliseconds (a constant `now_ms` disables the inter-byte timeout).
- [ ] Reads of a 16-bit ISR-owned tick take an atomic snapshot (see §4).
- [ ] Only one context calls `feed()` per parser.
- [ ] The frame handler never blocks the ISR that feeds the parser.
- [ ] Nothing is acted upon before `feed()` returns a frame (the CRC guarantee).
- [ ] Unknown channels are ignored, not treated as errors.
- [ ] The CRC is treated as a noise check, not as authentication (Threat model).
