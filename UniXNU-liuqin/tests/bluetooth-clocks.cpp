/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../drivers/PongBluetooth/PongBluetoothClocks.h"
#include <cassert>
#include <cstdio>
using namespace pongbt;
int main(){
 const ClockBranch master={0x2e004,1u<<2,1u<<1},slave={0x2e008,1u<<1,1u<<1},serial={0x2e610,1u<<9,0};
 assert(clockBranchReady(master,1u<<2,0x80000002));
 assert(clockBranchReady(slave,1u<<1,0x80000002));
 assert(!clockBranchReady(master,0,0x80000002));
 assert(!clockBranchReady(master,1u<<2,0x80000000));
 assert(!clockBranchReady(serial,1u<<9,0x80000002));
 assert(clockBranchReady(serial,1u<<9,0));
 assert(clockBranchReady(serial,1u<<9,0xa0000000));
 assert(!clockBranchReady(serial,0,0xa0000000));
 assert(!clockBranchReady(serial,1u<<9,0x90000000));
 assert(!clockBranchReady(master,1u<<2,0xffffffff));
 assert(gpll0MainHz(31,0x4000)==600000000);
 assert(gpll0Even300(31,0x4000,1u<<10));
 assert(!gpll0Even300(31,0,1u<<10));
 assert(!gpll0Even300(31,0x4000,0));
 assert(!gpll0Even300(31,0x4000,2u<<10));
 puts("PASS Bluetooth clocks: real vote plus AHB-only HWCG idle/branch2 ON contract, SE5 strictness and decoded fixed600/300MHz parent; host only.");
}
