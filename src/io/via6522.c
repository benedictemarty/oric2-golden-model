/**
 * @file via6522.c
 * @brief VIA 6522 implementation (stub)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include "io/via6522.h"
#include <string.h>

void via_init(via6522_t* via) {
    memset(via, 0, sizeof(via6522_t));
}

void via_reset(via6522_t* via) {
    via->ora = via->orb = 0;
    via->ddra = via->ddrb = 0;
    via->t1_counter = via->t1_latch = 0xFFFF;
    via->t2_counter = via->t2_latch = 0xFF;
    via->t1_running = via->t2_running = false;
    via->ifr = via->ier = 0;
}

uint8_t via_read(via6522_t* via, uint8_t reg) {
    /* TODO: Implement register reads */
    (void)via;
    (void)reg;
    return 0;
}

void via_write(via6522_t* via, uint8_t reg, uint8_t value) {
    /* TODO: Implement register writes */
    (void)via;
    (void)reg;
    (void)value;
}

void via_update(via6522_t* via, int cycles) {
    /* TODO: Implement timer updates */
    (void)via;
    (void)cycles;
}

void via_set_port_callbacks(via6522_t* via,
                            uint8_t (*porta_read)(void*),
                            void (*porta_write)(uint8_t, void*),
                            uint8_t (*portb_read)(void*),
                            void (*portb_write)(uint8_t, void*),
                            void* userdata) {
    via->porta_read = porta_read;
    via->porta_write = porta_write;
    via->portb_read = portb_read;
    via->portb_write = portb_write;
    via->userdata = userdata;
}

void via_set_irq_callback(via6522_t* via,
                         void (*callback)(bool, void*),
                         void* userdata) {
    via->irq_callback = callback;
    via->irq_userdata = userdata;
}

void via_trigger_ca1(via6522_t* via) {
    /* TODO: Implement CA1 interrupt */
    (void)via;
}

void via_trigger_ca2(via6522_t* via) {
    /* TODO: Implement CA2 interrupt */
    (void)via;
}

void via_trigger_cb1(via6522_t* via) {
    /* TODO: Implement CB1 interrupt */
    (void)via;
}

void via_trigger_cb2(via6522_t* via) {
    /* TODO: Implement CB2 interrupt */
    (void)via;
}
