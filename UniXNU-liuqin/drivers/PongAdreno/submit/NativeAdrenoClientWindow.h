/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
struct NativeAdrenoClientWindow {uint64_t start=0,bytes=0;};
// The two UI windows retain their established extents. Wide user windows
// begin above the permanently owned high-address CP probe at4GiB.
static inline unsigned nativeAdrenoClientCapacity(unsigned bits) {
    return bits==36?32:bits==32?16:0;
}
template<unsigned Count>
static NativeAdrenoClientWindow nativeAdrenoClientWindow(unsigned slot,unsigned bits=32) {
    static_assert(Count==16 || Count==32,"supported queue-scope tables");
    if(slot>=Count || slot>=nativeAdrenoClientCapacity(bits))return {};
    if(slot==0)return {0x20000000ull,0x50000000ull};
    if(slot==1)return {0x70000000ull,0x20000000ull};
    if(bits==36)return {0x110000000ull+uint64_t(slot-2)*0x10000000ull,0x10000000ull};
    return {0x90000000ull+uint64_t(slot-2)*0x08000000ull,0x08000000ull};
}
