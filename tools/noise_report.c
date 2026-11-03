/*
 * noise_report — measures LLP v4 behaviour over a noisy channel.
 *
 * Deterministic by construction (integer-only arithmetic, fixed seed), so the
 * generated table can be committed and checked in CI.
 *
 * For every scenario it builds a stream of frames, applies a noise model
 * (bit flips, dropped bytes, inserted bytes, bursts), runs the parser and
 * reports:
 *
 *   - frames delivered intact (unique, byte-identical to what was sent);
 *   - corrupted frames delivered (MUST be 0: the CRC guarantee);
 *   - error events and the average/max bytes needed to resynchronise.
 *
 * It also measures the false-accept rate by feeding random bytes that contain
 * no frames at all.
 *
 * Usage:
 *   cc -O2 -std=c99 -I include test/noise_report.c -o noise_report
 *   ./noise_report                # markdown table on stdout
 *   ./noise_report --update-doc   # rewrite docs/ROBUSTNESS.md section
 *   ./noise_report --check-doc    # fail if the documented table is stale
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llp_v4.h"

#define FRAME_COUNT 400
#define PAYLOAD_MIN 8
#define PAYLOAD_SIZE 64
#define STREAM_MAX (FRAME_COUNT * (10 + 2 * PAYLOAD_SIZE) + 1024)
#define NOISY_MAX (STREAM_MAX * 2 + 1024)
#define FALSE_ACCEPT_BYTES (16u * 1024u * 1024u)
#define DOC_PATH "docs/ROBUSTNESS.md"
#define DOC_BEGIN "<!-- LLP4-NOISE-TABLE:BEGIN -->"
#define DOC_END "<!-- LLP4-NOISE-TABLE:END -->"

typedef struct {
    const char *name;
    unsigned flip_per_100k;   /* probability of one flipped bit per byte */
    unsigned drop_per_100k;   /* probability of dropping a byte */
    unsigned insert_per_100k; /* probability of inserting a byte */
    unsigned burst_len;       /* bytes corrupted every 4096 bytes (0 = off) */
} scenario_t;

static const scenario_t SCENARIOS[] = {
    {"clean", 0, 0, 0, 0},
    {"bit flips 1e-5", 1, 0, 0, 0},
    {"bit flips 1e-4", 10, 0, 0, 0},
    {"bit flips 1e-3", 100, 0, 0, 0},
    {"drops 1e-4", 0, 10, 0, 0},
    {"inserts 1e-4", 0, 0, 10, 0},
    {"burst 8B / 4KB", 0, 0, 0, 8},
};
#define SCENARIO_COUNT (sizeof(SCENARIOS) / sizeof(SCENARIOS[0]))

static uint32_t rng_state = 0xC0FFEEu;

static uint32_t rng_next(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* ------------------------------------------------------------------------- */
/* Stream generation                                                         */
/* ------------------------------------------------------------------------- */

static uint8_t sent_payload[FRAME_COUNT][PAYLOAD_SIZE];
static uint8_t sent_len[FRAME_COUNT];
static uint8_t sent_channel[FRAME_COUNT];
static uint8_t sent_seen[FRAME_COUNT];

static size_t build_stream(uint8_t *stream)
{
    size_t offset = 0;
    unsigned i;

    rng_state = 0xC0FFEEu;
    for (i = 0; i < FRAME_COUNT; i++) {
        uint8_t len = (uint8_t)(PAYLOAD_MIN + rng_next() % (PAYLOAD_SIZE - PAYLOAD_MIN + 1u));
        unsigned j;
        size_t n;

        sent_payload[i][0] = (uint8_t)(i & 0xFFu);
        sent_payload[i][1] = (uint8_t)((i >> 8) & 0xFFu);
        for (j = 2; j < len; j++) {
            sent_payload[i][j] = (uint8_t)rng_next();
        }
        sent_len[i] = len;
        sent_channel[i] = (uint8_t)(i % 4u);

        n = llp4_encode(sent_channel[i], sent_payload[i], len, stream + offset,
                        STREAM_MAX - offset);
        if (n == 0) {
            fprintf(stderr, "encode failed\n");
            exit(1);
        }
        offset += n;
    }
    return offset;
}

/* ------------------------------------------------------------------------- */
/* Noise model                                                               */
/* ------------------------------------------------------------------------- */

static size_t apply_noise(const uint8_t *in, size_t in_len, uint8_t *out,
                          const scenario_t *sc)
{
    size_t i;
    size_t o = 0;

    rng_state = 0xBADF00Du;
    for (i = 0; i < in_len && o + 2 < NOISY_MAX; i++) {
        uint8_t byte = in[i];

        if (sc->drop_per_100k && (rng_next() % 100000u) < sc->drop_per_100k) {
            continue;
        }
        if (sc->insert_per_100k && (rng_next() % 100000u) < sc->insert_per_100k) {
            out[o++] = (uint8_t)rng_next();
        }
        if (sc->flip_per_100k && (rng_next() % 100000u) < sc->flip_per_100k) {
            byte ^= (uint8_t)(1u << (rng_next() % 8u));
        }
        if (sc->burst_len && (i % 4096u) < sc->burst_len) {
            byte = (uint8_t)rng_next();
        }
        out[o++] = byte;
    }
    return o;
}

/* ------------------------------------------------------------------------- */
/* Measurement                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    unsigned intact;
    unsigned corrupt;
    unsigned errors;
    unsigned resync_events;
    unsigned resync_sum;
    unsigned resync_max;
} result_t;

static void measure(const uint8_t *data, size_t len, result_t *r)
{
    llp4_parser_t parser;
    uint8_t payload[PAYLOAD_SIZE];
    size_t i;
    size_t last_error = 0;
    int pending_resync = 0;

    memset(r, 0, sizeof(*r));
    memset(sent_seen, 0, sizeof(sent_seen));
    llp4_parser_init(&parser, payload, PAYLOAD_SIZE, 2000);

    for (i = 0; i < len; i++) {
        int rc = llp4_parser_feed(&parser, data[i], (uint16_t)i);

        if (rc == LLP4_EV_FRAME) {
            uint8_t out_len = 0;
            const uint8_t *out = llp4_parser_payload(&parser, &out_len);
            unsigned seq = 0;

            if (pending_resync) {
                unsigned distance = (unsigned)(i - last_error);
                r->resync_events++;
                r->resync_sum += distance;
                if (distance > r->resync_max) {
                    r->resync_max = distance;
                }
                pending_resync = 0;
            }

            if (out_len < 2) {
                r->corrupt++;
                continue;
            }
            seq = (unsigned)out[0] | ((unsigned)out[1] << 8);
            if (seq >= FRAME_COUNT || out_len != sent_len[seq] ||
                llp4_parser_channel(&parser) != sent_channel[seq] ||
                memcmp(out, sent_payload[seq], out_len) != 0) {
                r->corrupt++;
                continue;
            }
            if (!sent_seen[seq]) {
                sent_seen[seq] = 1;
                r->intact++;
            }
        } else if (rc < 0) {
            r->errors++;
            last_error = i;
            pending_resync = 1;
        }
    }
}

static unsigned false_accepts(size_t bytes)
{
    llp4_parser_t parser;
    uint8_t payload[PAYLOAD_SIZE];
    size_t i;
    unsigned frames = 0;

    rng_state = 0x5EEDu;
    llp4_parser_init(&parser, payload, PAYLOAD_SIZE, 2000);
    for (i = 0; i < bytes; i++) {
        if (llp4_parser_feed(&parser, (uint8_t)rng_next(), 0) == LLP4_EV_FRAME) {
            frames++;
        }
    }
    return frames;
}

/* ------------------------------------------------------------------------- */
/* Output                                                                    */
/* ------------------------------------------------------------------------- */

static void run_all(char *table, size_t table_size)
{
    static uint8_t stream[STREAM_MAX];
    static uint8_t noisy[NOISY_MAX];
    static char buffer[8192];
    size_t stream_len = build_stream(stream);
    size_t used = 0;
    unsigned s;

    used += (size_t)snprintf(
        buffer + used, sizeof(buffer) - used,
        "| Scenario | Frames intact | Corrupt delivered | Errors | "
        "Avg bytes to resync | Max |\n"
        "|---|---:|---:|---:|---:|---:|\n");

    for (s = 0; s < SCENARIO_COUNT; s++) {
        result_t r;
        size_t noisy_len = apply_noise(stream, stream_len, noisy, &SCENARIOS[s]);
        unsigned avg;
        unsigned tenths;

        measure(noisy, noisy_len, &r);
        avg = (r.resync_events == 0)
                  ? 0
                  : (r.resync_sum + r.resync_events / 2u) / r.resync_events;
        /* Integer-only percentage so the table is identical on every libc. */
        tenths = (r.intact * 1000u + FRAME_COUNT / 2u) / FRAME_COUNT;

        used += (size_t)snprintf(
            buffer + used, sizeof(buffer) - used,
            "| %s | %u/%u (%u.%u%%) | %u | %u | %u | %u |\n",
            SCENARIOS[s].name, r.intact, FRAME_COUNT, tenths / 10u,
            tenths % 10u, r.corrupt, r.errors, avg, r.resync_max);
    }

    {
        unsigned false_frames = false_accepts(FALSE_ACCEPT_BYTES);
        used += (size_t)snprintf(
            buffer + used, sizeof(buffer) - used,
            "\nFalse accepts: **%u** frames delivered from %u MB of pure random "
            "bytes (expected < 1).\n",
            false_frames, FALSE_ACCEPT_BYTES / (1024u * 1024u));
    }

    snprintf(table, table_size, "%s", buffer);
}

static int read_doc_table(char *out, size_t out_size)
{
    FILE *f = fopen(DOC_PATH, "rb");
    char *text;
    long size;
    char *begin;
    char *end;

    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(f);
        return 0;
    }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return 0;
    }
    text[size] = '\0';
    fclose(f);

    begin = strstr(text, DOC_BEGIN);
    end = strstr(text, DOC_END);
    if (begin == NULL || end == NULL || end < begin) {
        free(text);
        return 0;
    }
    begin += strlen(DOC_BEGIN);
    {
        size_t len = (size_t)(end - begin);
        if (len >= out_size) {
            len = out_size - 1;
        }
        memcpy(out, begin, len);
        out[len] = '\0';
        /* Trim the blank lines around the table so the comparison is about
         * the content, not the formatting. */
        {
            size_t start = 0;
            while (out[start] == '\n' || out[start] == '\r') {
                start++;
            }
            if (start > 0) {
                memmove(out, out + start, strlen(out + start) + 1);
            }
            while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
                out[--len] = '\0';
            }
        }
    }
    free(text);
    return 1;
}

static int update_doc(const char *table)
{
    FILE *f = fopen(DOC_PATH, "rb");
    char *text;
    long size;
    char *begin;
    char *end;
    char *rebuilt;
    size_t head_len;
    size_t tail_len;
    size_t total;

    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", DOC_PATH);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)size + 1);
    if (text == NULL || fread(text, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(text);
        return 1;
    }
    text[size] = '\0';
    fclose(f);

    begin = strstr(text, DOC_BEGIN);
    end = strstr(text, DOC_END);
    if (begin == NULL || end == NULL) {
        free(text);
        fprintf(stderr, "%s: markers not found\n", DOC_PATH);
        return 1;
    }
    head_len = (size_t)(begin - text) + strlen(DOC_BEGIN);
    tail_len = strlen(end);
    total = head_len + strlen(table) + tail_len + 2;

    rebuilt = (char *)malloc(total);
    if (rebuilt == NULL) {
        free(text);
        return 1;
    }
    memcpy(rebuilt, text, head_len);
    rebuilt[head_len] = '\n';
    memcpy(rebuilt + head_len + 1, table, strlen(table));
    memcpy(rebuilt + head_len + 1 + strlen(table), end, tail_len + 1);

    f = fopen(DOC_PATH, "wb");
    if (f == NULL) {
        free(rebuilt);
        free(text);
        return 1;
    }
    fwrite(rebuilt, 1, strlen(rebuilt), f);
    fclose(f);
    free(rebuilt);
    free(text);
    return 0;
}

int main(int argc, char **argv)
{
    static char table[8192];

    run_all(table, sizeof(table));

    if (argc > 1 && strcmp(argv[1], "--update-doc") == 0) {
        if (update_doc(table) != 0) {
            return 1;
        }
        printf("%s updated\n", DOC_PATH);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--check-doc") == 0) {
        static char documented[8192];
        if (!read_doc_table(documented, sizeof(documented))) {
            fprintf(stderr, "%s: table markers not found\n", DOC_PATH);
            return 1;
        }
        if (strcmp(documented, table) != 0) {
            fprintf(stderr, "%s: noise table is stale; run --update-doc\n",
                    DOC_PATH);
            fprintf(stderr, "--- documented ---\n%s\n--- generated ---\n%s\n",
                    documented, table);
            return 1;
        }
        printf("%s: noise table matches the measurement\n", DOC_PATH);
        return 0;
    }

    fputs(table, stdout);
    return 0;
}
