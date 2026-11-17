#!/usr/bin/env python3
"""Host-only tests of actual pen event encoding; not BackBoard acceptance."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
source = (repo / 'userland/touch.c').read_text()
start = source.index('static int\nsend_pen(')
brace = source.index('{', start)
depth = 0
for end in range(brace, len(source)):
    depth += (source[end] == '{') - (source[end] == '}')
    if not depth:
        method = source[start:end+1]
        break

test = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "PongNativePenABI.h"
#include "PongPenEvent.h"
using IOHIDUserDeviceRef=void *;
using IOHIDServiceClientRef=void *;
struct Event {unsigned type=0,index=0,buttons=0,mask=0;double pressure=0,x=0,y=0,altitude=0,azimuth=0;bool range=false,touch=false,integrated=false,polar=false;Event *child=nullptr;};
using IOHIDEventRef=Event *;
using Boolean=bool;
constexpr unsigned DIGITIZER_RANGE=1,DIGITIZER_HAND=3;
constexpr unsigned FIELD_DIGITIZER_DISPLAY_INTEGRATED=0xb0019,FIELD_IS_BUILT_IN=4;
static unsigned events=0,dispatches=0;static bool accept=true;
static Event created[32];static Event *lastEvent=nullptr;
static IOHIDEventRef IOHIDEventCreateDigitizerEvent(void *,uint64_t,unsigned type,unsigned index,unsigned,unsigned change,unsigned mask,
    double px,double py,double,double p,double,uint32_t inRange,uint32_t isTouch,unsigned) {
    assert(events<32);auto *event=&created[events++];event->type=type;event->index=index;event->buttons=mask;event->mask=change;
    event->pressure=p;event->range=inRange;event->touch=isTouch;event->x=px;event->y=py;return event;
}
static IOHIDEventRef polarEvent(void *allocator,uint64_t time,unsigned index,unsigned identity,unsigned changes,unsigned buttons,
    double x,double y,double z,double pressure,double barrel,double,double altitude,double azimuth,uint32_t range,uint32_t invert,unsigned options) {
    assert(!invert);
    auto *event=IOHIDEventCreateDigitizerEvent(allocator,time,0,index,identity,changes,buttons,x,y,z,pressure,barrel,range,(options&0x20000)!=0,0);
    event->polar=true;event->altitude=altitude;event->azimuth=azimuth;return event;
}
static decltype(&polarEvent) IOHIDEventCreateDigitizerStylusEventWithPolarOrientation=nullptr;
static void IOHIDEventSetIntegerValue(IOHIDEventRef event,unsigned field,int value) {if(field==FIELD_DIGITIZER_DISPLAY_INTEGRATED)event->integrated=value;}
static void IOHIDEventAppendEvent(IOHIDEventRef parent,IOHIDEventRef child,unsigned){parent->child=child;}
static bool IOHIDVirtualServiceClientDispatchEvent(IOHIDServiceClientRef,IOHIDEventRef event){++dispatches;lastEvent=event;return accept;}
static void CFRelease(IOHIDEventRef) {}
'''
main = r'''
int main() {
    int token=0;
    pong_native_pen sample={};sample.time=123;sample.x=32768;sample.y=16384;sample.range=1;
    assert(send_pen(&token,&sample,nullptr)==0 && lastEvent->type==3 && lastEvent->child->type==0);
    assert(lastEvent->child->index==11 && lastEvent->integrated && lastEvent->child->integrated);
    assert(lastEvent->range && !lastEvent->touch && !lastEvent->child->touch && !lastEvent->child->buttons);
    auto hover=sample;sample.touch=1;sample.pressure=2048;sample.tiltX=45;sample.tiltY=0;sample.reserved=PONG_PEN_TILT_VALID;
    IOHIDEventCreateDigitizerStylusEventWithPolarOrientation=polarEvent;
    assert(send_pen(&token,&sample,&hover)==0 && lastEvent->touch && lastEvent->child->touch && lastEvent->child->buttons==1);
    assert(lastEvent->child->pressure==2048*500.0/4095.0 && lastEvent->child->x==32768/65535.0 && lastEvent->child->y==16384/65535.0);
    assert(lastEvent->child->polar && fabs(lastEvent->child->altitude-0.7853981633974483)<1e-12 && lastEvent->child->azimuth==0);
    assert((lastEvent->child->mask&2) && !(lastEvent->child->mask&1));
    auto previous=sample;sample.x++;
    assert(send_pen(&token,&sample,&previous)==0 && (lastEvent->child->mask&4) && !(lastEvent->child->mask&2));
    previous=sample;sample.tiltX=0;sample.tiltY=45;
    assert(send_pen(&token,&sample,&previous)==0 && fabs(lastEvent->child->azimuth-4.71238898038469)<1e-12);
    previous=sample;sample.tiltX=65;sample.tiltY=20;
    assert(send_pen(&token,&sample,&previous)==0 && lastEvent->child->polar && lastEvent->child->touch);
    previous=sample;sample.reserved=0; // missing orientation never drops real tip/pressure
    assert(send_pen(&token,&sample,&previous)==0 && !lastEvent->child->polar && lastEvent->child->touch);
    sample.touch=0;sample.range=0;sample.pressure=0;
    assert(send_pen(&token,&sample,&previous)==0 && !lastEvent->range && !lastEvent->touch && !lastEvent->child->buttons && !lastEvent->child->pressure);
    accept=false;assert(send_pen(&token,&sample,&previous)==-1);
    assert(send_pen(nullptr,&sample,nullptr)==-1);
    puts("PASS: screen collection/stylus child, transition-only touch mask,0..500 pressure and measured-tilt polar conversion/Touch option, missing-tilt pressure preserved; HOST MOCK, not app proof");
}
'''
with tempfile.TemporaryDirectory(prefix='native-pen-hid-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    cpp = temp / 'test.cpp'
    cpp.write_text(test + method + main)
    subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(repo / 'drivers/PongInputNative'), '-I', str(repo / 'userland'), str(cpp), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
