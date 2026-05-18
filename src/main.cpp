/**
 * Minimal LLP Protocol sketch — compiles on all supported platforms.
 * Demonstrates frame building and parsing for CI verification.
 */

#include <Arduino.h>
#include <llp_protocol.h>
#include <string.h>

static llp_parser_t parser;

void setup() {
    llp_parser_init(&parser);

    const char* msg = "Hello LLP";

    uint8_t final_payload[32];
    uint8_t tx_buffer[64];

    size_t payload_len = llp_build_final_payload(
        final_payload,
        sizeof(final_payload),
        (const uint8_t*)msg,
        (uint16_t)strlen(msg)
    );

    size_t frame_len = llp_build_frame(
        tx_buffer,
        sizeof(tx_buffer),
        final_payload,
        (uint16_t)payload_len
    );

    for (size_t i = 0; i < frame_len; i++) {
        llp_parser_process_byte(
            &parser,
            tx_buffer[i],
            millis()
        );
    }
}

void loop() {
}
