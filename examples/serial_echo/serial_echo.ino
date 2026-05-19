/**
 * serial_echo.ino — LLP Serial Echo Example
 *
 * Listens on Serial at 115200 baud, parses incoming LLP frames,
 * and responds with the frame status (OK/ERR) and unpacked payload.
 *
 * Always sends a periodic heartbeat so the host knows the device is alive.
 *
 * Wiring: connect UART RX/TX to host (USB-Serial adapter, PC, Raspberry Pi, etc.)
 */

#include "llp_protocol.h"

static llp_parser_t parser;
static unsigned long last_heartbeat = 0;
static const unsigned long HEARTBEAT_MS = 2000;

// Orphan-bytes tracking: bytes received that did not form a valid LLP frame
static uint16_t orphan_bytes = 0;
static unsigned long last_rx_byte_ms = 0;
static const unsigned long ORPHAN_TIMEOUT_MS = 300;

static void print_error_name(uint8_t code) {
    switch (code) {
        case LLP_ERR_OK:              Serial.print("OK"); break;
        case LLP_ERR_CHECKSUM:        Serial.print("CHECKSUM"); break;
        case LLP_ERR_PAYLOAD_LEN:     Serial.print("PAYLOAD_LEN"); break;
        case LLP_ERR_TIMEOUT:         Serial.print("TIMEOUT"); break;
        case LLP_ERR_SYNC:            Serial.print("SYNC"); break;
        case LLP_ERR_BUFFER_FULL:     Serial.print("BUFFER_FULL"); break;
        case LLP_ERR_TRANSFORM_LAYER: Serial.print("TRANSFORM_LAYER"); break;
        case LLP_ERR_MALFORMED_LAYER: Serial.print("MALFORMED_LAYER"); break;
        default:                      Serial.print("UNKNOWN"); break;
    }
}

static void send_heartbeat(void) {
    Serial.println("[HB] alive");
}

static void handle_valid_frame(llp_frame_t* frame) {
    uint8_t data[LLP_MAX_PAYLOAD];
    int len = llp_get_final_payload(frame, data, sizeof(data));

    if (len < 0) {
        Serial.println("ERR: no FinalNode in layer chain");
        return;
    }

    Serial.print("OK  len=");
    Serial.print(len);
    Serial.print("  hex=");
    for (int i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print('0');
        Serial.print(data[i], HEX);
    }

    Serial.print("  ascii=\"");
    for (int i = 0; i < len; i++) {
        char c = (char)data[i];
        Serial.print((c >= 0x20 && c < 0x7F) ? c : '.');
    }
    Serial.println("\"");
}

static void handle_error(llp_parser_t* p) {
    Serial.print("ERR code=0x");
    if (p->error_code < 0x10) Serial.print('0');
    Serial.print(p->error_code, HEX);
    Serial.print(" name=");
    print_error_name(p->error_code);
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { /* wait for USB Serial on native USB boards */ }

    llp_parser_init(&parser);

    Serial.println("[BOOT] LLP Serial Echo v3.0.0");
    Serial.println("[BOOT] Ready. Send LLP frames to this port.");
    send_heartbeat();
}

void loop() {
    unsigned long now = millis();

    // Process incoming serial bytes
    while (Serial.available() > 0) {
        uint8_t b = Serial.read();
        orphan_bytes++;
        last_rx_byte_ms = now;

        int result = llp_parser_process_byte(&parser, b, now);

        if (result == 1) {
            orphan_bytes = 0;
            handle_valid_frame(&parser.frame);
        } else if (result == -1) {
            orphan_bytes = 0;
            handle_error(&parser);
        }
    }

    // Warn if bytes were received but never formed a valid frame or error
    if (orphan_bytes > 0 && (long)(now - last_rx_byte_ms) >= (long)ORPHAN_TIMEOUT_MS) {
        Serial.print("WARN orphan_bytes=");
        Serial.print(orphan_bytes);
        Serial.println(" (no LLP frame detected)");
        orphan_bytes = 0;
    }

    // Periodic heartbeat
    if ((long)(now - last_heartbeat) >= (long)HEARTBEAT_MS) {
        last_heartbeat = now;
        send_heartbeat();
    }
}
