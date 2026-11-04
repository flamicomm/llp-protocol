/**
 * Minimal LLP v4 sketch — compiles on every supported platform.
 *
 * This is the Arduino entry point of the library repository: it exists so the
 * PlatformIO environments in platformio.ini build the v4 core for AVR, ESP8266,
 * ESP32 and STM32 (see also src/llp_v4_check.c).
 */

#include <Arduino.h>

#include "llp_v4.h"

static llp4_parser_t parser;
static uint8_t payload[64];

static void sink(void *ctx, uint8_t byte)
{
    (void)ctx;
    (void)byte;
}

void setup()
{
    uint8_t frame[128];
    size_t len;

    llp4_parser_init(&parser, payload, (uint8_t)sizeof(payload),
                     LLP4_DEFAULT_TIMEOUT_MS);

    len = llp4_encode(0, (const uint8_t *)"Hello LLP", 9, frame, sizeof(frame));
    (void)len;
}

void loop()
{
    /* Nothing to do: this sketch only checks that the library compiles. */
}
