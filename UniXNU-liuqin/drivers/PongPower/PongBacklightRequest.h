// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>

/* A single atomic snapshot: generation32 | target16 | fade-milliseconds16.
 * Producers never manipulate the timer or the hardware. */
static inline uint32_t pong_bl_target(uint64_t request) {
    return (uint32_t)((request >> 16) & 0xffffu);
}
static inline uint32_t pong_bl_fade(uint64_t request) {
    return (uint32_t)(request & 0xffffu);
}
static inline uint64_t pong_bl_next(uint64_t previous, uint32_t target, uint32_t fade) {
    return ((previous + (1ULL << 32)) & 0xffffffff00000000ULL) |
        ((uint64_t)target << 16) | (fade > 5000u ? 5000u : fade);
}

static inline int pong_bl_submit(uint64_t *mailbox, int64_t value, uint32_t fade,
    uint32_t maximum, bool relative, uint32_t minimum = 0) {
    if (minimum > maximum) minimum = maximum;
    uint64_t previous = __atomic_load_n(mailbox, __ATOMIC_ACQUIRE);
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        int64_t wanted = relative ? (int64_t)pong_bl_target(previous) + value : value;
        if (wanted < minimum) wanted = minimum;
        if (wanted > maximum) wanted = maximum;
        uint64_t next = pong_bl_next(previous, (uint32_t)wanted, fade);
        if (__atomic_compare_exchange_n(mailbox, &previous, next, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return (int)wanted;
    }
    return -1;
}
