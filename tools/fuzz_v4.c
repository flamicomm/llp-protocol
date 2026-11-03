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
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llp_v4.h"

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

#ifdef LLP4_FUZZ_MAIN
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    run_input(data, size, 0);
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
    return 0;
}
#endif
