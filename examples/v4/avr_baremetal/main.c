/*
 * avr_baremetal — reference transport adapter for AVR (ATmega328P, 16 MHz).
 *
 * Shows the three things a link has to provide:
 *
 *   1. RX: the UART interrupt pushes raw bytes into a ring; the main loop pops
 *      them and feeds the parser (the parser itself never touches hardware).
 *   2. TX: a sink writes frame bytes into a TX ring; the UDRE interrupt drains
 *      it. A frame is reserved as a whole before encoding (worst case
 *      `llp4_frame_size(len)`), so the wire never sees a partial frame.
 *   3. Time: a 1 ms tick is injected into `llp4_parser_feed()`.
 *
 * Protocol behaviour: channel 0 carries text lines ("PING" -> "PONG"),
 * channel 2 carries a 2-byte binary counter once per second.
 *
 * Build (CI does exactly this):
 *   avr-gcc -mmcu=atmega328p -Os -std=c99 -Wall -Wextra -I include \
 *           examples/v4/avr_baremetal/main.c -o /tmp/avr_baremetal.elf
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <string.h>

#include "llp_v4.h"
#include "llp_v4_ring.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define LINK_BAUD 115200UL
#define UBRR_VALUE ((F_CPU / (16UL * LINK_BAUD)) - 1UL)

#define RX_RING_SIZE 64u
#define TX_RING_SIZE 96u
#define PAYLOAD_MAX 64u
#define LINE_MAX 48u
#define TIMEOUT_MS 2000u

/* ------------------------------------------------------------------------- */
/* Link state                                                                */
/* ------------------------------------------------------------------------- */

static uint8_t rx_storage[RX_RING_SIZE];
static uint8_t tx_storage[TX_RING_SIZE];
static llp4_ring_t rx_ring;
static llp4_ring_t tx_ring;

static uint8_t payload[PAYLOAD_MAX];
static llp4_parser_t parser;

static uint8_t line[LINE_MAX];
static uint8_t line_len;

static volatile uint16_t ticks;
static uint16_t tx_dropped;

/* ------------------------------------------------------------------------- */
/* UART                                                                      */
/* ------------------------------------------------------------------------- */

static void uart_init(void)
{
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE & 0xFFu);
    UCSR0C = (uint8_t)((1u << UCSZ01) | (1u << UCSZ00)); /* 8N1 */
    UCSR0B = (uint8_t)((1u << RXEN0) | (1u << TXEN0) | (1u << RXCIE0));
}

ISR(USART_RX_vect)
{
    uint8_t byte = UDR0;
    (void)llp4_ring_push(&rx_ring, byte); /* dropped if the main loop lags */
}

ISR(USART_UDRE_vect)
{
    uint8_t byte;
    if (llp4_ring_pop(&tx_ring, &byte)) {
        UDR0 = byte;
    } else {
        UCSR0B &= (uint8_t)~(1u << UDRIE0);
    }
}

static void tx_kick(void)
{
    UCSR0B |= (1u << UDRIE0);
}

/* ------------------------------------------------------------------------- */
/* Time (1 ms tick on Timer1)                                                */
/* ------------------------------------------------------------------------- */

static void timer_init(void)
{
    TCCR1A = 0;
    TCCR1B = (uint8_t)((1u << WGM12) | (1u << CS11) | (1u << CS10)); /* CTC, /64 */
    OCR1A = (uint16_t)(F_CPU / 64UL / 1000UL - 1UL);
    TIMSK1 = (1u << OCIE1A);
}

ISR(TIMER1_COMPA_vect)
{
    ticks++;
}

/* ------------------------------------------------------------------------- */
/* Encoder                                                                   */
/* ------------------------------------------------------------------------- */

static void link_write(void *ctx, uint8_t byte)
{
    (void)ctx;
    if (!llp4_ring_push(&tx_ring, byte)) {
        tx_dropped++; /* link is not draining: count it, never block */
    }
}

/* Reserve the worst-case frame before encoding: all-or-nothing. */
static int tx_reserve(uint8_t len)
{
    uint16_t needed = (uint16_t)llp4_frame_size(len);
    uint16_t start = ticks;

    if (needed > llp4_ring_capacity(&tx_ring)) {
        return 0; /* will never fit: the caller must use smaller frames */
    }
    while (llp4_ring_free(&tx_ring) < needed) {
        tx_kick();
        if ((uint16_t)((uint16_t)ticks - start) > 20u) {
            return 0; /* bounded wait */
        }
    }
    return 1;
}

static int send_frame(uint8_t channel, const uint8_t *data, uint8_t len)
{
    llp4_tx_t tx;
    uint8_t i;

    if (!tx_reserve(len)) {
        tx_dropped++;
        return 0;
    }

    llp4_tx_begin(&tx, channel, len, link_write, 0);
    for (i = 0; i < len; i++) {
        llp4_tx_byte(&tx, data[i]);
    }
    llp4_tx_end(&tx);
    tx_kick();
    return 1;
}

static int send_text(uint8_t channel, const char *text)
{
    return send_frame(channel, (const uint8_t *)text, (uint8_t)strlen(text));
}

/* ------------------------------------------------------------------------- */
/* Application                                                               */
/* ------------------------------------------------------------------------- */

/* Channel 0 is a text protocol: lines terminated by '\n', assembled across
 * frames (a line may be split over several LLP frames). */
static void feed_line(uint8_t *data, uint8_t len)
{
    uint8_t i;

    for (i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\n' || c == '\r') {
            if (line_len == 0) {
                continue;
            }
            if (line_len == 4u && memcmp(line, "PING", 4) == 0) {
                (void)send_text(0, "PONG\n");
            } else {
                (void)send_text(0, "ERR\n");
            }
            line_len = 0;
        } else if (line_len < LINE_MAX) {
            line[line_len++] = c;
        } else {
            line_len = 0; /* overflow: drop the line */
        }
    }
}

static void handle_frame(uint8_t channel, const uint8_t *data, uint8_t len)
{
    switch (channel) {
    case 0:
        feed_line((uint8_t *)data, len);
        break;
    case 2:
        /* Binary telemetry counter is mirrored back on channel 2. */
        (void)send_frame(2, data, len);
        break;
    default:
        break; /* unknown channel: ignored, never a crash */
    }
}

static void poll_link(void)
{
    uint8_t byte;

    while (llp4_ring_pop(&rx_ring, &byte)) {
        if (llp4_parser_feed(&parser, byte, ticks) == LLP4_EV_FRAME) {
            handle_frame(llp4_parser_channel(&parser), payload, parser.length);
        }
    }
}

static void telemetry(void)
{
    static uint16_t last;
    uint16_t now = ticks;
    uint8_t report[2];

    if ((uint16_t)(now - last) < 1000u) {
        return;
    }
    last = now;
    report[0] = (uint8_t)(now & 0xFFu);
    report[1] = (uint8_t)(now >> 8);
    (void)send_frame(2, report, sizeof(report));
}

int main(void)
{
    llp4_ring_init(&rx_ring, rx_storage, (uint8_t)sizeof(rx_storage));
    llp4_ring_init(&tx_ring, tx_storage, (uint8_t)sizeof(tx_storage));
    llp4_parser_init(&parser, payload, (uint8_t)sizeof(payload), TIMEOUT_MS);

    uart_init();
    timer_init();
    sei();

    for (;;) {
        poll_link();
        telemetry();
    }

    return 0; /* never reached */
}
