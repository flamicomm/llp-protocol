/**
 * LLP Protocol v3.0.0 — Minimal UART Example (Arduino)
 *
 * Demonstrates the simplest usage of LLP v3:
 * - Receive frames via Serial (UART)
 * - Echo received data back
 *
 * Hardware: Any Arduino-compatible board
 * Baud rate: 115200
 */

#include "llp_protocol.h"

llp_parser_t parser;
uint8_t tx_buffer[LLP_MAX_FRAME_SIZE(LLP_MAX_PAYLOAD)];

void setup(void) {
    Serial.begin(115200);
    llp_parser_init(&parser);
}

void loop(void) {
    while (Serial.available()) {
        uint8_t byte = Serial.read();

        int result = llp_parser_process_byte(&parser, byte, millis());

        if (result == 1) {
            uint8_t data[LLP_MAX_PAYLOAD];
            int len = llp_get_final_payload(&parser.frame, data, sizeof(data));
            if (len >= 0) {
                send_data(data, (uint16_t)len);
            }
        }
    }
}

void send_data(const uint8_t* data, uint16_t len) {
    uint8_t llp_payload[LLP_MAX_PAYLOAD];
    size_t payload_len = llp_build_final_payload(llp_payload, sizeof(llp_payload),
                                                   data, len);
    size_t frame_len = llp_build_frame(tx_buffer, sizeof(tx_buffer),
                                        llp_payload, (uint16_t)payload_len);
    if (frame_len > 0) {
        Serial.write(tx_buffer, frame_len);
    }
}
