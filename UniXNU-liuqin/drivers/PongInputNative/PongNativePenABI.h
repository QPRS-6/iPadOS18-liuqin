/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_NATIVE_PEN_ABI_H
#define PONG_NATIVE_PEN_ABI_H
#include <stdint.h>
#define PONG_PEN_TILT_VALID 1u
struct pong_native_pen {
    uint64_t time;
    uint32_t range,touch,x,y,pressure,buttons;
    int32_t tiltX,tiltY; /* signed degrees; valid only when reserved & PONG_PEN_TILT_VALID */
    uint32_t distance,reserved;
};
#if defined(__cplusplus)
static_assert(sizeof(pong_native_pen)==48,"native pen queue ABI");
#else
_Static_assert(sizeof(struct pong_native_pen)==48,"native pen queue ABI");
#endif
#endif
