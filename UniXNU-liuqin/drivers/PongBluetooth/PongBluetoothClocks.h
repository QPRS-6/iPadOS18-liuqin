/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_BLUETOOTH_CLOCKS_H
#define PONG_BLUETOOTH_CLOCKS_H
#include <stdint.h>
namespace pongbt {
struct ClockBranch {unsigned offset,vote,hwcg;};
// gcc-sm8450.c on SM8475: only these AHB branches have HWCG bit1.
// clk-branch.c accepts a voted idle HWCG branch, or the normal branch2
// clock-on/NOC-FSM-on status. SE5 has no HWCG exemption.
inline bool clockBranchReady(const ClockBranch &b,uint32_t votes,uint32_t cbcr) {
    if((votes&b.vote)!=b.vote || cbcr==0xffffffffu)return false;
    return (b.hwcg && (cbcr&b.hwcg)) || !(cbcr&(1u<<31)) || ((cbcr>>28)&7)==2;
}
inline uint64_t gpll0MainHz(uint32_t l,uint32_t alpha) {
    return 19200000ULL*((l&0xffff)*65536ULL+(alpha&0xffff))/65536;
}
inline bool gpll0Even300(uint32_t l,uint32_t alpha,uint32_t user) {
    // Fixed Lucid OLE GPLL0 and the documented post-divider table: 1 -> /2.
    // Never program a guessed shared PLL rate/divider to obtain UART115200.
    return gpll0MainHz(l,alpha)==600000000ULL && ((user>>10)&15)==1;
}
}
#endif
