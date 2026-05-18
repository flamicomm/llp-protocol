#include <unity.h>
#include <string.h>
#include "llp_protocol.h"

static uint16_t crc_compute(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc = llp_crc16_update(crc, data[i]);
    }
    return crc;
}

void test_crc_known_values(void)
{
    uint16_t crc = llp_crc16_update(0xFFFF, 0x00);
    TEST_ASSERT_NOT_EQUAL_INT(0, crc);

    crc = llp_crc16_update(0xFFFF, 0xAA);
    uint16_t crc_aa = crc;
    crc = llp_crc16_update(crc, 0x55);
    TEST_ASSERT_NOT_EQUAL(crc_aa, crc);

    const uint8_t test_vect[] = {0x31, 0x32, 0x33, 0x34,
                                  0x35, 0x36, 0x37, 0x38, 0x39};
    crc = crc_compute(test_vect, sizeof(test_vect));
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);
}

void test_crc_differs_for_different_payloads(void)
{
    uint16_t crc_a = crc_compute((const uint8_t*)"HELLO", 5);
    uint16_t crc_b = crc_compute((const uint8_t*)"WORLD", 5);
    TEST_ASSERT_NOT_EQUAL(crc_a, crc_b);
}

void test_crc_detects_bit_flip(void)
{
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t crc_original = crc_compute(data, sizeof(data));

    data[2] ^= 0x01;
    uint16_t crc_flipped = crc_compute(data, sizeof(data));
    TEST_ASSERT_NOT_EQUAL(crc_original, crc_flipped);
}

void test_crc_detects_burst_error(void)
{
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint16_t crc_original = crc_compute(data, sizeof(data));

    data[3] ^= 0xFF;
    data[4] ^= 0xFF;
    data[5] ^= 0xFF;
    uint16_t crc_burst = crc_compute(data, sizeof(data));
    TEST_ASSERT_NOT_EQUAL(crc_original, crc_burst);
}

void test_crc_deterministic(void)
{
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint16_t crc1 = crc_compute(data, sizeof(data));
    uint16_t crc2 = crc_compute(data, sizeof(data));
    TEST_ASSERT_EQUAL_HEX16(crc1, crc2);
}

void test_crc_update_chain(void)
{
    uint16_t crc = 0xFFFF;
    crc = llp_crc16_update(crc, 0xAA);
    crc = llp_crc16_update(crc, 0x55);
    crc = llp_crc16_update(crc, 0x01);
    crc = llp_crc16_update(crc, 0x00);

    uint8_t all[] = {0xAA, 0x55, 0x01, 0x00};
    uint16_t crc_bulk = crc_compute(all, sizeof(all));

    TEST_ASSERT_EQUAL_HEX16(crc_bulk, crc);
}

void test_crc_order_matters(void)
{
    uint16_t crc_ab = crc_compute((const uint8_t*)"AB", 2);
    uint16_t crc_ba = crc_compute((const uint8_t*)"BA", 2);
    TEST_ASSERT_NOT_EQUAL(crc_ab, crc_ba);
}

void test_crc_empty_input(void)
{
    uint16_t crc = crc_compute(NULL, 0);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc);
}
