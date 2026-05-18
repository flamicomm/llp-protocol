#ifndef TEST_SPEC_COMMON_H
#define TEST_SPEC_COMMON_H

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "llp_protocol.h"

#define SPEC_MAX_FRAME  4096
#define SPEC_MAX_EVENTS 32

typedef struct {
    int type;
    int error_code;
    uint8_t payload[SPEC_MAX_FRAME];
    uint16_t payload_len;
} spec_event_t;

static size_t spec_hex_to_bytes(const char *hex, uint8_t *buf, size_t buf_size)
{
    if (!hex || !buf) return 0;
    size_t slen = strlen(hex);
    if (slen == 0) return 0;
    if (slen % 2 != 0) return 0;
    size_t count = slen / 2;
    if (count > buf_size) return 0;
    for (size_t i = 0; i < count; i++) {
        unsigned int b;
        sscanf(hex + 2 * i, "%02x", &b);
        buf[i] = (uint8_t)b;
    }
    return count;
}

static int spec_feed_frame(llp_parser_t *p, const uint8_t *data, size_t len,
                            int *last_result)
{
    int result = 0;
    for (size_t i = 0; i < len; i++) {
        result = llp_parser_process_byte(p, data[i], 0);
        if (result != 0) {
            if (last_result) *last_result = result;
        }
    }
    return result;
}

static int spec_feed_stream(llp_parser_t *p, const uint8_t *data, size_t len,
                             spec_event_t *events, int *event_count,
                             int max_events)
{
    *event_count = 0;
    for (size_t i = 0; i < len; i++) {
        int result = llp_parser_process_byte(p, data[i], 0);
        if (result == 1) {
            if (*event_count < max_events) {
                events[*event_count].type = 1;
                memcpy(events[*event_count].payload,
                       p->frame.payload, p->frame.payload_len);
                events[*event_count].payload_len = p->frame.payload_len;
                events[*event_count].error_code = 0;
                (*event_count)++;
            }
        } else if (result == -1) {
            if (*event_count < max_events) {
                events[*event_count].type = -1;
                events[*event_count].error_code = p->error_code;
                events[*event_count].payload_len = 0;
                (*event_count)++;
            }
        }
    }
    return *event_count;
}

static int spec_feed_stream_timed(llp_parser_t *p,
                                   const uint8_t *bytes,
                                   const unsigned long *times,
                                   size_t len,
                                   spec_event_t *events,
                                   int *event_count,
                                   int max_events)
{
    *event_count = 0;
    for (size_t i = 0; i < len; i++) {
        int result = llp_parser_process_byte(p, bytes[i], times[i]);
        if (result == 1) {
            if (*event_count < max_events) {
                events[*event_count].type = 1;
                memcpy(events[*event_count].payload,
                       p->frame.payload, p->frame.payload_len);
                events[*event_count].payload_len = p->frame.payload_len;
                events[*event_count].error_code = 0;
                (*event_count)++;
            }
        } else if (result == -1) {
            if (*event_count < max_events) {
                events[*event_count].type = -1;
                events[*event_count].error_code = p->error_code;
                events[*event_count].payload_len = 0;
                (*event_count)++;
            }
        }
    }
    return *event_count;
}

static void spec_concat_chunks(const char **chunks, int chunk_count,
                                uint8_t *out, size_t *out_len,
                                size_t max_len)
{
    *out_len = 0;
    for (int c = 0; c < chunk_count; c++) {
        uint8_t tmp[SPEC_MAX_FRAME];
        size_t tmp_len = spec_hex_to_bytes(chunks[c], tmp, sizeof(tmp));
        if (*out_len + tmp_len <= max_len) {
            memcpy(out + *out_len, tmp, tmp_len);
            *out_len += tmp_len;
        }
    }
}

#endif