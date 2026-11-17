/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_PEN_EVENT_H
#define PONG_PEN_EVENT_H
#include <math.h>
#include <stdbool.h>
#include "../drivers/PongInputNative/PongNativePenABI.h"

/* Event masks and units match WebKit's iOS IOKit SPI/HIDEventGenerator.
 * This transforms measured signed plane angles, not a synthesized sensor. */
static inline uint32_t pong_pen_change_mask(const struct pong_native_pen *p,const struct pong_native_pen *old) {
    uint32_t mask=0;
    if(!old || p->range!=old->range)mask|=1;
    if(!old || p->touch!=old->touch)mask|=2;
    if(!old || p->x!=old->x || p->y!=old->y)mask|=4;
    if(!old || p->pressure!=old->pressure || p->tiltX!=old->tiltX || p->tiltY!=old->tiltY || p->reserved!=old->reserved)mask|=0x40;
    if(p->range && (!old || !old->range))mask|=0x20;
    return mask;
}
static inline bool pong_pen_polar(const struct pong_native_pen *p,double *altitude,double *azimuth) {
    if(!(p->reserved&PONG_PEN_TILT_VALID) || p->tiltX<-90 || p->tiltX>90 || p->tiltY<-90 || p->tiltY>90)return false;
    const double pi=3.14159265358979323846;
    double tx=p->tiltX*pi/180.0,ty=p->tiltY*pi/180.0;
    double x=sin(tx)*cos(ty),y=sin(ty)*cos(tx),z=cos(tx)*cos(ty);
    /* HID's polar altitude is measured from the normal (UIKit uses its
     * complement); HID azimuth is reversed relative to screen coordinates. */
    *altitude=atan2(hypot(x,y),z);
    *azimuth=atan2(-y,x);if(*azimuth<0)*azimuth+=2*pi;
    return isfinite(*altitude) && isfinite(*azimuth);
}
#endif
