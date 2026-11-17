/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

// Demand classes supplied by XNU's effective QoS and scheduled priority.
enum {PongCpuBackground=0,PongCpuNormal=1,PongCpuLatency=2};
static inline int pongCpuKind(uint32_t midr)
{
  switch(midr&0xff0ffff0u) {
    case 0x410fd460u:return 0; // A510
    case 0x410fd470u:return 1; // A710
    case 0x410fd480u:return 2; // X2
    default:return -1;
  }
}
static inline uint64_t pongCpuPlacement(uint64_t available,const uint64_t *kinds,unsigned demand)
{
  // Only idle, online, recommended CPUs in the caller's pset are candidates.
  // Under load, use another kind rather than parking work behind a busy core.
  static const unsigned order[3][3]={{0,1,2},{1,2,0},{2,1,0}};
  if(demand>2)return available;
  for(unsigned i=0;i<3;i++)if(uint64_t eligible=available&kinds[order[demand][i]])return eligible;
  return available; // Unknown identity retains the scheduler's eligible set.
}
