// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "../drivers/PongPower/PongBacklightRequest.h"

static uint64_t mailbox;
static void *presses(void *) {
    for (unsigned i = 0; i < 100; i++)
        assert(pong_bl_submit(&mailbox, 1, 0, 2047, true) >= 0);
    return nullptr;
}
int main() {
    mailbox = (uint64_t)1009 << 16;
    assert(pong_bl_submit(&mailbox, 137, 0, 2047, true) == 1146);
    assert(pong_bl_submit(&mailbox, -137, 0, 2047, true) == 1009);
    assert(pong_bl_submit(&mailbox, -1, 0, 2047, false) == 0);
    assert(pong_bl_submit(&mailbox, 9999, 9999, 2047, false) == 2047);
    assert(pong_bl_fade(mailbox) == 5000);
    assert(pong_bl_submit(&mailbox, 0, 0, 2047, false, 102) == 102);
    assert(pong_bl_submit(&mailbox, -137, 0, 2047, true, 102) == 102);
    assert(pong_bl_submit(&mailbox, 0, 0, 2047, false) == 0); // explicit internal off
    uint64_t old = mailbox;
    assert(pong_bl_submit(&mailbox, 900, 0, 2047, false) == 900);
    uint64_t rollback = pong_bl_next(old, 1009, 0);
    assert(!__atomic_compare_exchange_n(&mailbox, &old, rollback, false,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    assert(pong_bl_target(mailbox) == 900); // failure cannot erase a newer request
    mailbox = 0;
    pthread_t threads[4];
    for (auto &thread : threads) assert(!pthread_create(&thread, nullptr, presses, nullptr));
    for (auto &thread : threads) assert(!pthread_join(thread, nullptr));
    assert(pong_bl_target(mailbox) == 400);
    assert((mailbox >> 32) == 400);
    puts("PASS: limits, fades, rollback and concurrent relative presses");
}
