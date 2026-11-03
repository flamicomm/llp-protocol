/*
 * host_posix — reference transport adapter for POSIX file descriptors.
 *
 * The link is just a file descriptor (a pipe, a PTY, a TCP socket). This
 * example shows the whole adapter in ~40 lines (serve_fd) plus a self-test
 * that exercises the framing under noise.
 *
 * Usage:
 *   ./host_posix --selftest      # encode/decode + noise, no I/O (used by CI)
 *   ./host_posix --stdio         # read LLP from stdin, reply to stdout
 *   ./host_posix --tcp PORT      # one-client-at-a-time LLP server
 *
 * Build:
 *   cc -Os -std=c99 -Wall -Wextra -I include \
 *      examples/v4/host_posix/host_posix.c -o /tmp/host_posix
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "llp_v4.h"

#define MAX_FRAME 300
#define MAX_FRAMES 64
#define PAYLOAD_CAP 255

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static uint32_t rng_state = 0x12345678u;

static uint32_t rng_next(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

typedef struct {
    uint8_t channel;
    uint8_t len;
    uint8_t data[256];
} frame_t;

static frame_t sent[MAX_FRAMES];
static size_t sent_count;
static frame_t got[MAX_FRAMES];
static size_t got_count;

/* Encode a frame into `out`; returns the number of bytes written. */
static size_t encode_frame(uint8_t *out, size_t cap, uint8_t channel,
                           const uint8_t *data, uint8_t len)
{
    return llp4_encode(channel, data, len, out, cap);
}

/* Feed a whole buffer through a parser, recording delivered frames. */
static void parse_buffer(const uint8_t *data, size_t len, llp4_parser_t *parser,
                         int record)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (llp4_parser_feed(parser, data[i], (uint16_t)i) == LLP4_EV_FRAME) {
            if (record && got_count < MAX_FRAMES) {
                frame_t *f = &got[got_count++];
                uint8_t out_len = 0;
                const uint8_t *payload = llp4_parser_payload(parser, &out_len);
                f->channel = llp4_parser_channel(parser);
                f->len = out_len;
                memcpy(f->data, payload, out_len);
            }
        }
    }
}

static int frames_equal(const frame_t *a, const frame_t *b)
{
    return a->channel == b->channel && a->len == b->len &&
           memcmp(a->data, b->data, a->len) == 0;
}

static int frame_was_sent(const frame_t *f)
{
    size_t i;
    for (i = 0; i < sent_count; i++) {
        if (frames_equal(f, &sent[i])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Self-test                                                                 */
/* ------------------------------------------------------------------------- */

static int selftest_roundtrip(void)
{
    int i;

    for (i = 0; i < 300; i++) {
        uint8_t payload[64];
        uint8_t frame[MAX_FRAME];
        uint8_t len = (uint8_t)(rng_next() % (sizeof(payload) + 1));
        uint8_t channel = (uint8_t)rng_next();
        size_t n;
        uint8_t j;
        llp4_parser_t parser;
        uint8_t buf[PAYLOAD_CAP];

        for (j = 0; j < len; j++) {
            uint32_t r = rng_next() % 4u;
            payload[j] = (r == 0) ? 0xAA : (r == 1) ? 0x00 : (r == 2) ? 0x55
                                                          : (uint8_t)rng_next();
        }

        n = encode_frame(frame, sizeof(frame), channel, payload, len);
        if (n == 0) {
            fprintf(stderr, "encode failed (len=%u)\n", len);
            return 1;
        }

        sent_count = 0;
        sent[0].channel = channel;
        sent[0].len = len;
        memcpy(sent[0].data, payload, len);
        sent_count = 1;

        got_count = 0;
        llp4_parser_init(&parser, buf, sizeof(buf), 2000);
        parse_buffer(frame, n, &parser, 1);

        if (got_count != 1 || !frames_equal(&got[0], &sent[0])) {
            fprintf(stderr, "roundtrip mismatch at iteration %d (len=%u)\n", i,
                    len);
            return 1;
        }
    }
    return 0;
}

static int selftest_noise(void)
{
    uint8_t stream[8192];
    uint8_t noisy[8192 + 32];
    size_t count = 0;
    size_t noisy_len;
    int i;
    llp4_parser_t parser;
    uint8_t buf[PAYLOAD_CAP];

    sent_count = 0;
    for (i = 0; i < 20; i++) {
        uint8_t payload[32];
        uint8_t len = (uint8_t)(1 + rng_next() % sizeof(payload));
        uint8_t j;
        size_t n;

        for (j = 0; j < len; j++) {
            payload[j] = (uint8_t)(i * 16 + j);
        }
        n = encode_frame(stream + count, sizeof(stream) - count, (uint8_t)i,
                         payload, len);
        if (n == 0) {
            return 1;
        }
        sent[sent_count].channel = (uint8_t)i;
        sent[sent_count].len = len;
        memcpy(sent[sent_count].data, payload, len);
        sent_count++;
        count += n;
    }

    /* Inject noise: flips, one dropped byte, some inserted garbage. */
    memcpy(noisy, stream, count);
    noisy_len = count;
    for (i = 0; i < 6; i++) {
        noisy[rng_next() % noisy_len] ^= (uint8_t)(1u << (rng_next() % 8u));
    }
    {
        size_t drop_at = rng_next() % noisy_len;
        memmove(noisy + drop_at, noisy + drop_at + 1, noisy_len - drop_at - 1);
        noisy_len--;
    }
    for (i = 0; i < 4; i++) {
        size_t at = rng_next() % noisy_len;
        memmove(noisy + at + 1, noisy + at, noisy_len - at);
        noisy[at] = (uint8_t)rng_next();
        noisy_len++;
    }

    got_count = 0;
    llp4_parser_init(&parser, buf, sizeof(buf), 2000);
    parse_buffer(noisy, noisy_len, &parser, 1);

    /* Guarantee: every delivered frame is byte-identical to one that was sent
     * (no truncated or corrupted frame ever reaches the application). */
    for (i = 0; i < (int)got_count; i++) {
        if (!frame_was_sent(&got[i])) {
            fprintf(stderr, "noise test: delivered a frame that was never sent\n");
            return 1;
        }
    }

    /* After the damage the parser must have recovered a good part of the
     * stream; the exact number is deterministic for the fixed seed. */
    if (got_count < 8) {
        fprintf(stderr, "noise test: only %zu/%zu frames recovered\n", got_count,
                sent_count);
        return 1;
    }

    printf("noise test: %zu/%zu frames delivered intact after noise\n",
           got_count, sent_count);
    return 0;
}

static int selftest(void)
{
    rng_state = 0x12345678u;
    if (selftest_roundtrip() != 0) {
        return 1;
    }
    printf("roundtrip: 300 frames OK\n");
    if (selftest_noise() != 0) {
        return 1;
    }
    printf("selftest OK\n");
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Live adapter                                                              */
/* ------------------------------------------------------------------------- */

/* The whole POSIX adapter: read bytes, feed the parser, answer on channel 0. */
static void serve_fd(int fd_in, int fd_out)
{
    uint8_t chunk[256];
    uint8_t payload[PAYLOAD_CAP];
    llp4_parser_t parser;
    ssize_t n;
    size_t i;

    llp4_parser_init(&parser, payload, sizeof(payload), 2000);

    while ((n = read(fd_in, chunk, sizeof(chunk))) > 0) {
        for (i = 0; i < (size_t)n; i++) {
            if (llp4_parser_feed(&parser, chunk[i], 0) == LLP4_EV_FRAME) {
                uint8_t len = 0;
                const uint8_t *data = llp4_parser_payload(&parser, &len);
                uint8_t out[MAX_FRAME];
                size_t out_len;
                const char *reply;

                fprintf(stderr, "frame: channel=%u len=%u\n",
                        (unsigned)llp4_parser_channel(&parser), (unsigned)len);

                reply = (len == 4 && memcmp(data, "PING", 4) == 0) ? "PONG\n"
                                                                  : "ERR\n";
                out_len = encode_frame(out, sizeof(out), llp4_parser_channel(&parser),
                                       (const uint8_t *)reply,
                                       (uint8_t)strlen(reply));
                if (out_len > 0 && write(fd_out, out, out_len) != (ssize_t)out_len) {
                    return;
                }
            }
        }
    }
}

static int run_tcp(int port)
{
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    struct sockaddr_in addr;
    int client;
    socklen_t len = sizeof(addr);

    if (server < 0) {
        perror("socket");
        return 1;
    }
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(server);
        return 1;
    }
    listen(server, 1);
    fprintf(stderr, "listening on 127.0.0.1:%d\n", port);

    for (;;) {
        client = accept(server, (struct sockaddr *)&addr, &len);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }
        serve_fd(client, client);
        close(client);
    }
    close(server);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "--selftest") == 0) {
        return selftest();
    }
    if (strcmp(argv[1], "--stdio") == 0) {
        serve_fd(STDIN_FILENO, STDOUT_FILENO);
        return 0;
    }
    if (strcmp(argv[1], "--tcp") == 0 && argc == 3) {
        return run_tcp(atoi(argv[2]));
    }

    fprintf(stderr,
            "usage: %s [--selftest | --stdio | --tcp PORT]\n",
            argv[0]);
    return 2;
}
