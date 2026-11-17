/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "submit/AdrenoPM4.h"

struct PongGpuVaPolicy {unsigned bits;uint64_t bytes;size_t table_pages,leaves;};
static inline PongGpuVaPolicy pongGpuVaPolicy(unsigned requested)
{
    // Fixed native4K topology: L1 root, one L2 per GiB, one L3 per2MiB.
    if(requested==36)return {36,UINT64_C(1)<<34,1+16+8192,size_t(1)<<22};
    return {32,UINT64_C(1)<<32,1+4+2048,size_t(1)<<20};
}
constexpr uint64_t PongGpuHighProbeIova=UINT64_C(0x100000000);
constexpr uint32_t PongGpuHighProbeValue=0x73640036;
// Two real CP packets: fetch the MEM_WRITE IB above4GiB, write another page
// above4GiB. CPU confirmation and a real ring fence are separate prerequisites.
static inline bool pongGpuHighProbePackets(uint64_t base,uint32_t *ib,uint32_t *launch)
{
    if(!ib || !launch || base<UINT64_C(0x100000000) || (base&4095) || base>UINT64_MAX-8192)return false;
    if(AdrenoPM4Type7(0x3d,3,ib)!=AdrenoPM4Success || AdrenoPM4Type7(0x3f,3,launch)!=AdrenoPM4Success)return false;
    ib[1]=uint32_t(base+4096);ib[2]=uint32_t((base+4096)>>32);ib[3]=PongGpuHighProbeValue;
    launch[1]=uint32_t(base);launch[2]=uint32_t(base>>32);launch[3]=4;
    return true;
}
