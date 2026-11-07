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

Timeouts must be `<= 32767 ms`. No timeout is reported while waiting for the
magic, so an idle link is normal.

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
- [ ] Only one context calls `feed()` per parser.
- [ ] The frame handler never blocks the ISR that feeds the parser.
- [ ] Nothing is acted upon before `feed()` returns a frame (the CRC guarantee).
- [ ] Unknown channels are ignored, not treated as errors.
