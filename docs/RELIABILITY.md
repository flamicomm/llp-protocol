# Profile R1 — reliable command channel (C implementation)

The **normative** definition of Profile R1 lives in the specification
repository:

> **<https://github.com/flamicomm/llp-spec/blob/main/docs/RELIABILITY.md>**

This page documents the C implementation shipped here
([`../include/llp_v4_rel.h`](../include/llp_v4_rel.h)) and how to wire it into a
link. It does not add requirements: when in doubt, the specification wins.

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

/* on an ACK frame from channel 2 */
llp4_rel_tx_on_ack(&tx, ack_seq);
```

`llp4_rel_tx_start()` returns 0 while the window is busy; with a window of one
that only happens if the application sends faster than the link acknowledges.

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
| `llp4_rel_tx_t` | 74 (63-byte payload + bookkeeping) |
| `llp4_rel_rx_t` | 6 |
| Flash | 0 when a side is unused (all functions are `static inline`) |

A receiver-only target (the AVR firmware) only instantiates the 6-byte state.

## Tests

[`../test/test_v4_rel.c`](../test/test_v4_rel.c) implements the eight
conformance traces of the specification (§11) plus the unit behaviour:
retransmission keeps the payload byte-identical, exhaustion never reports
delivery, stale ACKs are ignored, the sequence wraps at 256, and the profile
payloads round-trip through the real wire format.

The wire layout is also pinned in
[`../spec/vectors/v4/encode.json`](../spec/vectors/v4/encode.json)
(`enc-rel-command-seq0`, `enc-rel-command-seq-stuffed`, `enc-rel-ack-seq7`).

## Non-goals

Reliability for the receiver → sender direction, flow control, and several
concurrent reliable streams. See §13 of the specification.
