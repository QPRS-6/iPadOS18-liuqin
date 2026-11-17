/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>
enum : uint32_t {PongLogDefaultFlushes=240,PongLogMaximumFlushes=720};
// Preserve the normal write budget. Explicit diagnostic boots may extend it
// to about an hour; malformed requests never remove the finite write limit.
static inline uint32_t pongLogFlushBudget(uint32_t requested) {
    return requested && requested<=PongLogMaximumFlushes?requested:PongLogDefaultFlushes;
}
