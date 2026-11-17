// SPDX-License-Identifier: MIT
#include "../drivers/PongAdreno/submit/NativeAdrenoClientWindow.h"
#include <cassert>
#include <cstdio>
int main() {
    // Every possible admission order has the same disjoint assignment;
    // exhausted early poster reservations cannot starve the last free slot.
    uint64_t total=0;
    for(unsigned i=0;i<16;i++) {
        const auto a=nativeAdrenoClientWindow<16>(i);
        assert(a.bytes && !(a.start&4095) && a.start+a.bytes<=0x100000000ull);
        total+=a.bytes;
        for(unsigned j=0;j<i;j++) {
            const auto b=nativeAdrenoClientWindow<16>(j);
            assert(a.start>=b.start+b.bytes || b.start>=a.start+a.bytes);
        }
    }
    assert(total==0xe0000000ull);
    assert(nativeAdrenoClientWindow<16>(0).bytes==0x50000000ull);
    assert(nativeAdrenoClientWindow<16>(1).bytes==0x20000000ull);
    assert(nativeAdrenoClientWindow<16>(15).bytes==0x08000000ull);
    assert(nativeAdrenoClientWindow<16>(15).start==0xf8000000ull);
    assert(nativeAdrenoClientWindow<16>(16).bytes==0);
    assert(nativeAdrenoClientWindow<16>(~0u).bytes==0);
    puts("PASS all16 disjoint bounded scopes, unchanged UI windows, guaranteed last128MiB slot and invalid-index refusal");
}
