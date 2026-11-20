# Profile R1 — reliable command channel (C implementation)

The **normative** definition of Profile R1 lives in the specification
repository:

> **<https://github.com/flamicomm/llp-spec/blob/main/docs/RELIABILITY.md>**

This page documents the C implementation shipped here
([`../include/llp_v4_rel.h`](../include/llp_v4_rel.h)) and how to wire it into a
link. It does not add requirements: when in doubt, the specification wins.

A Python implementation of the same two state machines lives in
[`../scripts/llp_v4_rel.py`](../scripts/llp_v4_rel.py) (vendored from llp-spec's
reference) and is used by serial tooling such as `grbl-llp`'s `stream_gcode.py`.

## What the header provides

| Piece | Type | Responsibility |
|---|---|---|
| Sender | `llp4_rel_tx_t` | stop-and-wait window of one, byte-identical retransmission, exhaustion reporting |
| Receiver | `llp4_rel_rx_t` | duplicate suppression (one `last_seq`), counters |

It deliberately does **not** do framing (that is `llp_v4.h`), timing (the caller
injects a 16-bit millisecond clock) or buffering of application data.

```
COMMAND (channel 1):  [SEQ][DATA...]      sender -> receiver
ACK     (channel 2):  [SEQ]               receiver -> sender
HELLO   (channel 3):  [SESSION]           sender -> receiver, resets dedup state
```

## Session start (important)

`SEQ` is one byte, so a **restarted sender** (reboot, reconnect) would restart
its sequence and could collide with the receiver's `last_seq`: the first command
would be ACKed and silently dropped. Before the first command of a session the
sender must emit a hello on channel 3 (twice is recommended: it is not
acknowledged), and the receiver must reset its dedup state when it sees one:

```c
/* channel 3 frame from the sender */
llp4_rel_rx_init(&rx);      /* forget last_seq: new session */
```

## Sender recipe

```c
llp4_rel_tx_t tx;
uint8_t seq = 0;

llp4_rel_tx_init(&tx, LLP4_REL_DEFAULT_ACK_TIMEOUT_MS,
                 LLP4_REL_DEFAULT_MAX_RETRIES, LLP4_REL_DEFAULT_BACKOFF_MS);

/* new command */
if (llp4_rel_tx_start(&tx, seq++, data, data_len, now_ms)) {
    send_frame(LLP4_REL_CH_COMMAND, llp4_rel_tx_payload(&tx, NULL));
}

/* every main-loop iteration */
switch (llp4_rel_tx_service(&tx, now_ms)) {
case LLP4_REL_TX_SEND:
    send_frame(LLP4_REL_CH_COMMAND, llp4_rel_tx_payload(&tx, NULL));
    break;
case LLP4_REL_TX_EXHAUSTED:
    report_to_application();      /* never claim delivery */
    break;
default:
    break;
}

/* on a frame from channel 2: an ACK carries exactly one byte (§3) */
llp4_rel_tx_on_ack_frame(&tx, channel, payload, len);
```

`llp4_rel_tx_start()` returns 0 while the window is busy; with a window of one
that only happens if the application sends faster than the link acknowledges.

### The one-byte ACK rule

§3 of the specification is a MUST on the sender side: *"A receiver MUST ignore
an ACK frame whose length is not 1."* A frame on channel 2 with `LEN == 0` or
`LEN > 1` is malformed (or belongs to another protocol that reuses the channel)
and must not release the window. `llp4_rel_tx_on_ack_frame()` applies both
checks — channel 2 **and** `len == 1` — and then defers to
`llp4_rel_tx_on_ack()`, so no caller has to restate the rule:

```c
/* equivalent to the two checks above, without the helper */
if (channel == LLP4_REL_CH_ACK && len == 1) {
    llp4_rel_tx_on_ack(&tx, payload[0]);
}
```

### Configuration guards

`llp4_rel_tx_init()` clamps out-of-contract configuration once, in init, so a
typo cannot change behaviour later (init-only cost, nothing in the per-event
path):

| Input | Clamped to | Why |
|---|---|---|
| `max_retries == 0` | `LLP4_REL_MIN_RETRIES` (1) | 0 means "exhaust with zero retransmissions", outside the normative 1–10 of §5.4 |
| `max_retries > 10` | `LLP4_REL_MAX_RETRIES` (10) | upper end of the normative range |
| `timeout_ms == 0` / `backoff_ms == 0` | their defaults | a zero timer would fire on every poll |
| timer `> 32767` | `LLP4_REL_MAX_TIMER_MS` (32767) | the 16-bit wrap-safe difference cannot reach a larger value, so a 65535 ms timer would never elapse |

## Receiver recipe

```c
llp4_rel_rx_t rx;
uint8_t ack = seq;

llp4_rel_rx_init(&rx);

/* frame from channel 1, payload = [seq][data...] */
if (len >= 1 && capacity_available(len - 1)) {   /* check FIRST: §6.1 */
    if (llp4_rel_rx_accept(&rx, payload[0])) {
        deliver(payload + 1, len - 1);
    }
    send_frame(LLP4_REL_CH_ACK, &ack, 1);        /* ACK new and duplicate */
}
```

The capacity check **before** `llp4_rel_rx_accept()` is the only subtle rule: if
the data does not fit, the sequence number must not be consumed and the frame
must not be acknowledged, so the sender retries later. That turns buffer
pressure into a retransmission instead of a dropped command.

## Memory

| Item | Bytes |
|---|---:|
| `llp4_rel_tx_t` | 76 on AVR (63-byte payload + bookkeeping) |
| `llp4_rel_rx_t` | 6 |
| Flash | 0 when a side is unused (all functions are `static inline`) |

`llp4_rel_tx_t` is 76 B on AVR, where the struct has no padding, and 78 B on a
64-bit host, where `uint16_t` needs two-byte alignment (one pad byte before
`sent_ms`, one of tail padding). Both figures are gated: `sizeof(llp4_rel_tx_t)`
is asserted at compile time in [`../src/llp_v4_check.c`](../src/llp_v4_check.c)
(`== 76` on AVR, `<= 78` elsewhere) and `python3 scripts/measure_v4.py --budget`
fails if the AVR figure grows past 76 B.

A receiver-only target (the AVR firmware) only instantiates the 6-byte state.

## Tests

[`../test/test_v4_rel.c`](../test/test_v4_rel.c) implements the nine
conformance traces of the specification (§11) plus the unit behaviour:
retransmission keeps the payload byte-identical, exhaustion never reports
delivery, stale ACKs are ignored, the sequence wraps at 256, and the profile
payloads round-trip through the real wire format. It also pins the guards of
this page: `max_retries` clamped into 1–10 (never "exhausted with zero
retransmissions"), timers clamped to 32767, and an ACK frame with `len != 1`
leaving the window untouched.

The wire layout is also pinned in
[`../spec/vectors/v4/encode.json`](../spec/vectors/v4/encode.json)
(`enc-rel-command-seq0`, `enc-rel-command-seq-stuffed`, `enc-rel-ack-seq7`).

## Non-goals

Reliability for the receiver → sender direction, flow control, and several
concurrent reliable streams. See §13 of the specification.
