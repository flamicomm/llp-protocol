/*
 * llp_v4_check.c — compile-time smoke test for the v4 core on the target.
 *
 * This translation unit exists so every PlatformIO environment in
 * platformio.ini builds the v4 header for its architecture (AVR with PROGMEM
 * CRC table, ESP8266/ESP32, STM32, host). It adds no runtime behaviour.
 */

#include "llp_v4.h"

/* The low-RAM profile must stay tiny on the smallest targets. 17 bytes on AVR
 * with stats disabled; the bound leaves room for alignment differences. */
#if LLP4_ENABLE_STATS
typedef char llp4_parser_size_check[(sizeof(llp4_parser_t) <= 32) ? 1 : -1];
#else
typedef char llp4_parser_size_check[(sizeof(llp4_parser_t) <= 24) ? 1 : -1];
#endif

/* Exercised by the conformance tests on the host; here it only has to link. */
static llp4_parser_t llp4_check_parser;
static uint8_t llp4_check_payload[64];

void llp_v4_compile_check(void)
{
    llp4_parser_init(&llp4_check_parser, llp4_check_payload,
                     (uint8_t)sizeof(llp4_check_payload),
                     LLP4_DEFAULT_TIMEOUT_MS);
    (void)llp4_parser_feed(&llp4_check_parser, 0xAA, 0);
    (void)llp4_parser_feed(&llp4_check_parser, 0x55, 1);
}
