/*
 * llp_v4_ring.h — optional single-producer/single-consumer byte ring.
 *
 * Not part of the protocol: this is the small container most links need
 * between an interrupt handler and the main loop. It is provided so the
 * examples (and applications) do not have to re-derive the index arithmetic.
 *
 * Contract:
 *   - one context pushes (`head`), another pops (`tail`);
 *   - indices are 8-bit, so the ring holds at most 254 bytes and every read
 *     of a shared index is atomic on an 8-bit MCU;
 *   - the two contexts must not call push()/pop() concurrently with each
 *     other's role (that is what SPSC means).
 */

#ifndef LLP_V4_RING_H
#define LLP_V4_RING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;          /**< caller-owned storage, `size` bytes */
    uint8_t size;          /**< total slots, 2..255 (capacity = size - 1) */
    volatile uint8_t head; /**< written by the producer */
    volatile uint8_t tail; /**< written by the consumer */
} llp4_ring_t;

/** Configure a ring. `size` must be >= 2 and <= 255. */
static inline void llp4_ring_init(llp4_ring_t *r, uint8_t *buf, uint8_t size)
{
    r->buf = buf;
    r->size = size;
    r->head = 0;
    r->tail = 0;
}

/** Usable capacity (one slot is reserved to tell full from empty). */
static inline uint8_t llp4_ring_capacity(const llp4_ring_t *r)
{
    return (uint8_t)(r->size - 1u);
}

/** Bytes currently stored. Conservative if the other context is active. */
static inline uint8_t llp4_ring_count(const llp4_ring_t *r)
{
    uint8_t head = r->head;
    uint8_t tail = r->tail;
    if (head >= tail) {
        return (uint8_t)(head - tail);
    }
    return (uint8_t)(r->size - tail + head);
}

/** Free slots. Conservative if the other context is active. */
static inline uint8_t llp4_ring_free(const llp4_ring_t *r)
{
    return (uint8_t)(llp4_ring_capacity(r) - llp4_ring_count(r));
}

/** Push one byte. Returns 1 on success, 0 when full. */
static inline int llp4_ring_push(llp4_ring_t *r, uint8_t byte)
{
    uint8_t next = (uint8_t)(r->head + 1u);
    if (next == r->size) {
        next = 0;
    }
    if (next == r->tail) {
        return 0;
    }
    r->buf[r->head] = byte;
    r->head = next;
    return 1;
}

/** Pop one byte. Returns 1 on success, 0 when empty. */
static inline int llp4_ring_pop(llp4_ring_t *r, uint8_t *byte)
{
    uint8_t tail = r->tail;
    if (tail == r->head) {
        return 0;
    }
    *byte = r->buf[tail];
    tail++;
    if (tail == r->size) {
        tail = 0;
    }
    r->tail = tail;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* LLP_V4_RING_H */
