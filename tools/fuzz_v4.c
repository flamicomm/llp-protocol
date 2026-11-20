/*
 * fuzz_v4 — property-based fuzzer for the LLP v4 core.
 *
 * Works in two modes:
 *
 *   - default: a standalone, deterministic random campaign (no dependencies)
 *       cc -fsanitize=address,undefined -g -I include test/fuzz_v4.c -o fuzz
 *       ./fuzz [seed] [iterations]
 *
 *   - libFuzzer:  -DLLP4_FUZZ_MAIN with clang -fsanitize=fuzzer,address
 *
 * Invariants checked on every input:
 *
 *   1. The parser never writes outside the payload buffer (guard bytes).
 *   2. A delivered frame always has length <= capacity and points at the
 *      caller's buffer.
 *   3. A delivered frame is exactly reproducible: re-encoding its payload and
 *      feeding that frame back yields the same channel and payload.
 *   4. The error code returned matches `p->error` and is one of the four.
 *   5. After any input, feeding a known-good frame is always accepted
 *      (the parser always resynchronises).
 *
 * A second campaign (`run_rel_input`, `ring_fuzz`) drives Profile R1 and the
 * SPSC ring with random interleavings of command / ACK / HELLO events and
 * push/pop operations. Its invariants:
 *
 *   6. A frame emitted by the R1 sender round-trips through the real wire and
 *      the bytes the receiver would deliver are the command, never corrupted.
 *   7. Retransmissions are byte-identical, bounded by `max_retries`, and the
 *      sender never reports exhaustion before spending them or twice at the
 *      same millisecond.
 *   8. The ring behaves as a capacity-(size-1) FIFO: push/pop agree with the
 *      shadow model and never reorder or lose a byte.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llp_v4.h"
#include "llp_v4_rel.h"
#include "llp_v4_ring.h"

#define GUARD 32u
#define CAP_MAX 255u
#define FRAME_MAX (10u + 2u * CAP_MAX)

static uint8_t payload_buf[GUARD + CAP_MAX + GUARD];

static uint32_t rng_state = 1u;

static uint32_t rng_next(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static void fail(const char *what, size_t iteration)
{
    fprintf(stderr, "INVARIANT FAILED: %s (iteration %zu)\n", what, iteration);
    exit(1);
}

static void paint_guards(void)
{
    memset(payload_buf, 0xA5, sizeof(payload_buf));
}

static int guards_intact(void)
{
    size_t i;
    for (i = 0; i < GUARD; i++) {
        if (payload_buf[i] != 0xA5u) {
            return 0;
        }
        if (payload_buf[GUARD + CAP_MAX + i] != 0xA5u) {
            return 0;
        }
    }
    return 1;
}

/* Invariant 3: the delivered payload must round-trip byte for byte. */
static void check_roundtrip(const llp4_parser_t *p, const uint8_t *payload,
                            uint8_t len, size_t iteration)
{
    uint8_t frame[FRAME_MAX];
    uint8_t back[CAP_MAX];
    llp4_parser_t q;
    size_t n;
    size_t i;
    int frames = 0;

    n = llp4_encode(p->channel, payload, len, frame, sizeof(frame));
    if (n == 0) {
        fail("re-encode failed", iteration);
    }

    llp4_parser_init(&q, back, (uint8_t)sizeof(back), 2000);
    for (i = 0; i < n; i++) {
        if (llp4_parser_feed(&q, frame[i], (uint16_t)i) == LLP4_EV_FRAME) {
            frames++;
            if (q.length != len || q.channel != p->channel ||
                memcmp(back, payload, len) != 0) {
                fail("roundtrip mismatch", iteration);
            }
        }
    }
    if (frames != 1) {
        fail("roundtrip delivered the wrong number of frames", iteration);
    }
}

static void run_input(const uint8_t *data, size_t size, size_t iteration)
{
    llp4_parser_t p;
    uint8_t cap;
    uint16_t now = 0;
    size_t i;
    size_t attempt;
    static const uint8_t good[] = {0xAA, 0x55, 0x00, 0x00, 0x23, 0xB3}; /* empty payload */

    /* Capacity derived from the input: exercise the LEN rejection path and
     * the full 255-byte range. */
    cap = (size > 0) ? (uint8_t)(data[0] % 8u == 0u ? 0u : data[0]) : 32u;

    paint_guards();
    llp4_parser_init(&p, payload_buf + GUARD, cap, 2000);

    for (i = 1; i < size; i++) {
        uint16_t delta = (uint16_t)(((i & 31u) == 0u) ? 3000u : (data[i] & 0x0Fu));
        int rc;

        now = (uint16_t)(now + delta);
        rc = llp4_parser_feed(&p, data[i], now);

        if (rc == LLP4_EV_FRAME) {
            uint8_t len = 0;
            const uint8_t *payload = llp4_parser_payload(&p, &len);

            if (len > cap) {
                fail("delivered a payload longer than the capacity", iteration);
            }
            if (payload != payload_buf + GUARD) {
                fail("payload pointer is not the caller buffer", iteration);
            }
            check_roundtrip(&p, payload, len, iteration);
        } else if (rc < 0) {
            if (rc < -(int)LLP4_ERR_SYNC || rc > -(int)LLP4_ERR_CRC) {
                fail("invalid error code", iteration);
            }
            if (p.error != (uint8_t)(-rc)) {
                fail("p.error does not match the return value", iteration);
            }
        } else if (p.error != LLP4_OK) {
            fail("stale error after a successful byte", iteration);
        }

        if (!guards_intact()) {
            fail("parser wrote outside the payload buffer", iteration);
        }
    }

    /* Invariant 5: a good frame is eventually accepted. A stray 0xAA at the
     * end of the noise can consume the next byte as an escape continuation, so
     * the sender may lose exactly one frame; the following one must get
     * through. */
    for (attempt = 0; attempt < 2u; attempt++) {
        for (i = 0; i < sizeof(good); i++) {
            now = (uint16_t)(now + 1u);
            if (llp4_parser_feed(&p, good[i], now) == LLP4_EV_FRAME) {
                return; /* resynchronised: OK */
            }
        }
    }
    fail("parser did not resynchronise on a repeated good frame", iteration);
}

/* ------------------------------------------------------------------------- */
/* Profile R1 + ring: random interleavings                                   */
/* ------------------------------------------------------------------------- */

#ifndef LLP4_FUZZ_REL_ITERS
#define LLP4_FUZZ_REL_ITERS 20000u
#endif

/* Cap the bytes a single interleaving consumes, so 20 000 inputs stay cheap
 * under ASan while still mixing many events per input. */
#define REL_INPUT_MAX 128u
#define REL_RING_MAX 32u

typedef struct {
    uint8_t data[LLP4_REL_MAX_DATA];
    uint8_t len;
} rel_cmd_t;

typedef struct {
    llp4_rel_tx_t tx;
    llp4_rel_rx_t rx;
    rel_cmd_t outstanding;
    uint8_t out_seq;
    uint8_t sends;
    uint16_t now;
    uint16_t last_exhaust_ms;
    uint8_t have_exhaust;
} rel_state_t;

/* Invariants 6 and 7: encode the outstanding payload with the real wire,
 * parse it back and feed the R1 receiver, then optionally ACK.
 * `send_ack == 0` models a lost ACK, which forces a retransmission later. */
static void rel_wire_deliver(rel_state_t *s, int send_ack, size_t iteration)
{
    uint8_t frame[10u + 2u * LLP4_REL_MAX_PAYLOAD];
    uint8_t payload[LLP4_REL_MAX_PAYLOAD];
    uint8_t parse_buf[LLP4_REL_MAX_PAYLOAD];
    llp4_parser_t parser;
    uint16_t t = 0;
    uint8_t len = 0;
    const uint8_t *body;
    size_t n;
    size_t i;
    int delivered = 0;

    if (!s->tx.active) {
        fail("R1: delivery requested with an idle window", iteration);
    }

    body = llp4_rel_tx_payload(&s->tx, &len);
    if (len == 0 || len > LLP4_REL_MAX_PAYLOAD) {
        fail("R1: outstanding payload has an invalid length", iteration);
    }
    memcpy(payload, body, len);

    n = llp4_encode(LLP4_REL_CH_COMMAND, payload, len, frame, sizeof(frame));
    if (n == 0) {
        fail("R1: re-encode of the outstanding payload failed", iteration);
    }

    llp4_parser_init(&parser, parse_buf, (uint8_t)sizeof(parse_buf), 2000);
    for (i = 0; i < n; i++) {
        if (llp4_parser_feed(&parser, frame[i], t++) == LLP4_EV_FRAME) {
            uint8_t got_len = 0;
            const uint8_t *got = llp4_parser_payload(&parser, &got_len);
            int accepted;

            if (llp4_parser_channel(&parser) != LLP4_REL_CH_COMMAND) {
                fail("R1: command arrived on the wrong channel", iteration);
            }
            if (got_len == 0 || got[0] != s->out_seq) {
                fail("R1: command SEQ changed on the wire", iteration);
            }
            if ((uint8_t)(got_len - 1u) != s->outstanding.len ||
                memcmp(got + 1, s->outstanding.data, s->outstanding.len) != 0) {
                fail("R1: delivered DATA differs from the command", iteration);
            }

            accepted = llp4_rel_rx_accept(&s->rx, got[0]);
            if (!accepted &&
                (!s->rx.have_seq || s->rx.last_seq != got[0])) {
                fail("R1: duplicate suppression misclassified a frame",
                     iteration);
            }

            if (send_ack) {
                /* Same call a bridge makes when it dispatches channel 2. */
                (void)llp4_rel_tx_on_ack_frame(&s->tx, LLP4_REL_CH_ACK, got, 1);
            }
            delivered = 1;
            break;
        }
    }
    if (!delivered) {
        fail("R1: the sender's own frame did not parse back", iteration);
    }
}

static void run_rel_input(const uint8_t *data, size_t size, size_t iteration)
{
    rel_state_t s;
    uint8_t next_seq = 1;
    size_t n = (size > REL_INPUT_MAX) ? REL_INPUT_MAX : size;
    size_t i;

    memset(&s, 0, sizeof(s));
    llp4_rel_tx_init(&s.tx, 50,
                     (uint8_t)((n > 0) ? 1u + (data[0] % 3u) : 1u),
                     LLP4_REL_DEFAULT_BACKOFF_MS);
    llp4_rel_rx_init(&s.rx);

    for (i = 0; i < n; i++) {
        switch (data[i] % 5u) {
        case 0: { /* load a command (the application calls start) */
            if (!s.tx.active) {
                rel_cmd_t candidate;
                uint8_t j;

                candidate.len =
                    (uint8_t)(data[(i + 1u) % n] % (LLP4_REL_MAX_DATA + 1u));
                for (j = 0; j < candidate.len; j++) {
                    candidate.data[j] = data[(i + 2u + j) % n];
                }
                if (llp4_rel_tx_start(&s.tx, next_seq, candidate.data,
                                      candidate.len, s.now)) {
                    s.outstanding = candidate;
                    s.out_seq = next_seq++;
                    s.sends = 0;
                    rel_wire_deliver(&s, (data[i] & 0x80u) != 0, iteration);
                }
            }
            break;
        }
        case 1: { /* poll the retransmission timer */
            uint16_t step = (uint16_t)(((data[i] >> 3) & 0x1Fu) + 1u);
            int rc;

            s.now = (uint16_t)(s.now + step);
            rc = llp4_rel_tx_service(&s.tx, s.now);
            if (rc == LLP4_REL_TX_SEND) {
                uint8_t len = 0;
                const uint8_t *p = llp4_rel_tx_payload(&s.tx, &len);

                if (len != (uint8_t)(s.outstanding.len + 1u) ||
                    p[0] != s.out_seq ||
                    memcmp(p + 1, s.outstanding.data, s.outstanding.len) != 0) {
                    fail("R1: retransmission is not byte-identical", iteration);
                }
                s.sends++;
                if (s.sends > s.tx.max_retries) {
                    fail("R1: more retransmissions than max_retries",
                         iteration);
                }
                rel_wire_deliver(&s, (data[i] & 0x40u) != 0, iteration);
            } else if (rc == LLP4_REL_TX_EXHAUSTED) {
                if (s.tx.retries != s.tx.max_retries) {
                    fail("R1: exhaustion before spending the retries",
                         iteration);
                }
                if (s.have_exhaust && s.now == s.last_exhaust_ms) {
                    fail("R1: two exhaustion events in one millisecond",
                         iteration);
                }
                s.have_exhaust = 1;
                s.last_exhaust_ms = s.now;
            }
            break;
        }
        case 2: /* ACK with an arbitrary (usually stale) SEQ: no-op */
            (void)llp4_rel_tx_on_ack(&s.tx, (uint8_t)(data[i] ^ 0x5Au));
            break;
        case 3: /* ACK the outstanding frame through the frame helper */
            if (s.tx.active) {
                (void)llp4_rel_tx_on_ack_frame(&s.tx, LLP4_REL_CH_ACK,
                                               &s.out_seq, 1);
            }
            break;
        default: /* HELLO (channel 3): a new session resets dedup (T9) */
            llp4_rel_rx_init(&s.rx);
            break;
        }
    }
}

/* Invariant 8: the ring is a capacity-(size-1) FIFO, bit for bit. */
static void ring_fuzz(const uint8_t *data, size_t size, size_t iteration)
{
    uint8_t storage[REL_RING_MAX];
    uint8_t model[REL_RING_MAX];
    uint8_t ring_size = (uint8_t)((size > 0) ? 2u + (data[0] % 30u) : 2u);
    uint8_t capacity = (uint8_t)(ring_size - 1u);
    uint8_t count = 0;
    uint8_t tail = 0;
    llp4_ring_t ring;
    size_t i;

    llp4_ring_init(&ring, storage, ring_size);
    if (llp4_ring_capacity(&ring) != capacity) {
        fail("ring: capacity mismatch after init", iteration);
    }

    for (i = 1; i < size; i++) {
        if ((data[i] & 1u) != 0u) {
            uint8_t value = (uint8_t)(data[i] * 31u + 7u);
            int pushed = llp4_ring_push(&ring, value);

            if ((count < capacity) != (pushed != 0)) {
                fail("ring: push disagrees with the full predicate", iteration);
            }
            if (pushed) {
                model[(uint8_t)((tail + count) % capacity)] = value;
                count++;
            }
        } else {
            uint8_t value = 0;
            int popped = llp4_ring_pop(&ring, &value);

            if ((count > 0) != (popped != 0)) {
                fail("ring: pop disagrees with the empty predicate", iteration);
            }
            if (popped) {
                if (value != model[tail]) {
                    fail("ring: FIFO order corrupted", iteration);
                }
                tail = (uint8_t)((tail + 1u) % capacity);
                count--;
            }
        }

        if (llp4_ring_count(&ring) != count) {
            fail("ring: count diverged from the model", iteration);
        }
        if (llp4_ring_free(&ring) != (uint8_t)(capacity - count)) {
            fail("ring: free diverged from the model", iteration);
        }
    }

    /* A disabled ring stays a safe no-op. */
    llp4_ring_init(&ring, NULL, ring_size);
    if (llp4_ring_capacity(&ring) != 0u ||
        llp4_ring_push(&ring, 0xAAu) != 0) {
        fail("ring: invalid init is not disabled", iteration);
    }
}

#ifdef LLP4_FUZZ_MAIN
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    run_input(data, size, 0);
    run_rel_input(data, size, 0);
    ring_fuzz(data, size, 0);
    return 0;
}
#else

static void fill_random(uint8_t *data, size_t size, unsigned mode)
{
    size_t i;
    for (i = 0; i < size; i++) {
        uint32_t r = rng_next() % 100u;
        switch (mode) {
        case 0: /* uniform bytes */
            data[i] = (uint8_t)rng_next();
            break;
        case 1: /* ASCII text lines, what a CNC actually sends */
            data[i] = (r < 70u) ? (uint8_t)(' ' + (rng_next() % 90u))
                                : (r < 90u) ? (uint8_t)'\n'
                                             : (uint8_t)(rng_next() & 0x7Fu);
            break;
        case 2: /* magic / escape heavy */
            data[i] = (r < 40u) ? 0xAAu : (r < 60u) ? 0x55u
                                  : (r < 80u) ? 0x00u
                                              : (uint8_t)rng_next();
            break;
        default: { /* valid frames mixed with garbage */
            uint8_t payload[32];
            uint8_t len = (uint8_t)(rng_next() % sizeof(payload));
            size_t j;
            size_t n;
            for (j = 0; j < len; j++) {
                payload[j] = (uint8_t)rng_next();
            }
            n = llp4_encode((uint8_t)rng_next(), payload, len, data + i,
                            size - i);
            if (n == 0) {
                data[i] = (uint8_t)rng_next(); /* no room left: garbage byte */
                break;
            }
            i += n - 1u; /* the loop increment consumes the last byte */
            break;
        }
        }
    }
}

int main(int argc, char **argv)
{
    uint32_t iterations = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0)
                                     : 200000u;
    uint32_t i;
    uint8_t data[512];

    rng_state = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 0) : 1u;
    if (rng_state == 0) {
        rng_state = 0x9E3779B9u;
    }

    for (i = 0; i < iterations; i++) {
        size_t size = rng_next() % sizeof(data);
        memset(data, 0, sizeof(data));
        fill_random(data, size, i % 4u);
        run_input(data, size, i);
    }

    printf("fuzz OK: %u inputs, 4 distributions, 5 invariants\n", iterations);

    /* Profile R1 + ring campaign: same distributions, random interleavings of
     * command / ACK / HELLO and push / pop. */
    for (i = 0; i < (uint32_t)LLP4_FUZZ_REL_ITERS; i++) {
        size_t size = rng_next() % sizeof(data);
        memset(data, 0, sizeof(data));
        fill_random(data, size, i % 4u);
        run_rel_input(data, size, i);
        ring_fuzz(data, size, i);
    }

    printf("rel/ring OK: %u inputs, 3 invariants (R1 delivery, retries, ring FIFO)\n",
           (unsigned)LLP4_FUZZ_REL_ITERS);
    return 0;
}
#endif
