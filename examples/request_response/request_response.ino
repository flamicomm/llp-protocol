/**
 * LLP Protocol v3.0.0 — Request-Response Pattern (Arduino)
 *
 * Demonstrates a request-response pattern with timeout and retries.
 * Since v3.0.0 removed type/id from the transport layer, this example
 * implements a simple app-level protocol:
 *   FinalNode payload: [id_L][id_H][command_bytes...]
 */

#include "llp_protocol.h"

#define ACK_TIMEOUT_MS 1000
#define MAX_RETRIES 3
#define MAX_PENDING 5

typedef struct {
    uint16_t id;
    unsigned long sent_time;
    uint8_t retries;
    uint8_t frame[LLP_MAX_FRAME_SIZE(LLP_MAX_PAYLOAD)];
    size_t frame_len;
    bool in_use;
} pending_req_t;

llp_parser_t parser;
uint8_t tx_buffer[LLP_MAX_FRAME_SIZE(LLP_MAX_PAYLOAD)];
pending_req_t pending[MAX_PENDING];
static uint16_t next_id = 1;

void setup(void) {
    Serial.begin(9600);
    llp_parser_init(&parser);
    memset(pending, 0, sizeof(pending));
}

void loop(void) {
    while (Serial.available()) {
        uint8_t byte = Serial.read();
        int result = llp_parser_process_byte(&parser, byte, millis());
        if (result == 1) {
            handle_frame(&parser.frame);
        }
    }
    check_timeouts();

    static unsigned long last_cmd = 0;
    if (millis() - last_cmd > 5000) {
        last_cmd = millis();
        send_request((const uint8_t*)"HELLO", 5);
    }
}

void handle_frame(llp_frame_t* frame) {
    uint8_t data[LLP_MAX_PAYLOAD];
    int len = llp_get_final_payload(frame, data, sizeof(data));
    if (len <= 0) return;

    uint16_t rx_id = (uint16_t)data[0] | ((uint16_t)data[1] << 8);

    for (int i = 0; i < MAX_PENDING; i++) {
        if (pending[i].in_use && pending[i].id == rx_id) {
            pending[i].in_use = false;
            break;
        }
    }
}

void check_timeouts(void) {
    unsigned long now = millis();
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!pending[i].in_use) continue;
        if (now - pending[i].sent_time < ACK_TIMEOUT_MS) continue;

        if (pending[i].retries < MAX_RETRIES) {
            pending[i].retries++;
            pending[i].sent_time = now;
            Serial.write(pending[i].frame, pending[i].frame_len);
        } else {
            pending[i].in_use = false;
        }
    }
}

void send_request(const uint8_t* cmd, uint16_t cmd_len) {
    int slot = -1;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!pending[i].in_use) { slot = i; break; }
    }
    if (slot < 0) return;

    uint16_t id = next_id++;

    uint8_t app_data[LLP_MAX_PAYLOAD];
    uint16_t app_len = cmd_len + 2;
    if (app_len > LLP_MAX_PAYLOAD) return;
    app_data[0] = (uint8_t)(id & 0xFF);
    app_data[1] = (uint8_t)((id >> 8) & 0xFF);
    memcpy(&app_data[2], cmd, cmd_len);

    uint8_t llp_payload[LLP_MAX_PAYLOAD];
    size_t payload_len = llp_build_final_payload(llp_payload, sizeof(llp_payload),
                                                   app_data, app_len);
    size_t frame_len = llp_build_frame(tx_buffer, sizeof(tx_buffer),
                                        llp_payload, (uint16_t)payload_len);
    if (frame_len == 0) return;

    pending[slot].id = id;
    pending[slot].sent_time = millis();
    pending[slot].retries = 0;
    pending[slot].frame_len = frame_len;
    pending[slot].in_use = true;
    memcpy(pending[slot].frame, tx_buffer, frame_len);

    Serial.write(tx_buffer, frame_len);
}
