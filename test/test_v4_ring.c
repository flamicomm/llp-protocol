/* llp_v4_ring — single-producer/single-consumer ring tests. */

#include <unity.h>

#include "llp_v4_ring.h"

static uint8_t storage[8];
static llp4_ring_t ring;

void test_v4_ring_init(void)
{
    llp4_ring_init(&ring, storage, sizeof(storage));
    TEST_ASSERT_EQUAL_UINT8(7, llp4_ring_capacity(&ring));
    TEST_ASSERT_EQUAL_UINT8(0, llp4_ring_count(&ring));
    TEST_ASSERT_EQUAL_UINT8(7, llp4_ring_free(&ring));
}

void test_v4_ring_push_pop(void)
{
    uint8_t byte = 0;

    llp4_ring_init(&ring, storage, sizeof(storage));
    TEST_ASSERT_EQUAL_INT(1, llp4_ring_push(&ring, 0x42));
    TEST_ASSERT_EQUAL_UINT8(1, llp4_ring_count(&ring));
    TEST_ASSERT_EQUAL_INT(1, llp4_ring_pop(&ring, &byte));
    TEST_ASSERT_EQUAL_HEX8(0x42, byte);
    TEST_ASSERT_EQUAL_UINT8(0, llp4_ring_count(&ring));
}

void test_v4_ring_fills_to_capacity(void)
{
    uint8_t i;
    uint8_t byte;

    llp4_ring_init(&ring, storage, sizeof(storage));
    for (i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_push(&ring, i));
    }
    TEST_ASSERT_EQUAL_UINT8(0, llp4_ring_free(&ring));
    TEST_ASSERT_EQUAL_INT(0, llp4_ring_push(&ring, 0xFF));

    for (i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_pop(&ring, &byte));
        TEST_ASSERT_EQUAL_UINT8(i, byte);
    }
    TEST_ASSERT_EQUAL_INT(0, llp4_ring_pop(&ring, &byte));
}

void test_v4_ring_wraps_indices(void)
{
    uint8_t byte;
    uint8_t i;

    llp4_ring_init(&ring, storage, sizeof(storage));

    /* Fill and drain several times over the same storage to force the
     * head/tail to wrap around the end of the buffer. */
    for (i = 0; i < 40; i++) {
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_push(&ring, (uint8_t)(0xA0 + i)));
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_pop(&ring, &byte));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(0xA0 + i), byte);
    }

    /* Interleave so head and tail are at different positions when wrapping. */
    for (i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_push(&ring, i));
    }
    for (i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_pop(&ring, &byte));
        TEST_ASSERT_EQUAL_UINT8(i, byte);
    }
    for (i = 5; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_push(&ring, i));
    }
    TEST_ASSERT_EQUAL_UINT8(7, llp4_ring_count(&ring));
    for (i = 3; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(1, llp4_ring_pop(&ring, &byte));
        TEST_ASSERT_EQUAL_UINT8(i, byte);
    }
}

void test_v4_ring_smallest_size(void)
{
    uint8_t tiny[2];
    uint8_t byte = 0;

    llp4_ring_init(&ring, tiny, sizeof(tiny)); /* capacity 1 */
    TEST_ASSERT_EQUAL_UINT8(1, llp4_ring_capacity(&ring));
    TEST_ASSERT_EQUAL_INT(1, llp4_ring_push(&ring, 0x11));
    TEST_ASSERT_EQUAL_INT(0, llp4_ring_push(&ring, 0x22));
    TEST_ASSERT_EQUAL_INT(1, llp4_ring_pop(&ring, &byte));
    TEST_ASSERT_EQUAL_HEX8(0x11, byte);
    TEST_ASSERT_EQUAL_INT(0, llp4_ring_pop(&ring, &byte));
}
