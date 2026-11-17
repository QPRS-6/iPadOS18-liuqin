// SPDX-License-Identifier: MIT
#include "../drivers/ArmCpu/PongCpuPlacement.h"
#include <cstdio>
#include <cstdlib>
#define check(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::_Exit(1);}}while(0)
int main() {
  const uint64_t kinds[]={0xf,0x70,0x80};unsigned cases=0;
  for(uint64_t available=0;available<256;available++)for(unsigned demand=0;demand<3;demand++) {
    const uint64_t picked=pongCpuPlacement(available,kinds,demand);
    check(!(picked&~available));check(bool(picked)==bool(available));
    if(demand==PongCpuBackground && (available&0xf))check(picked==(available&0xf));
    if(demand==PongCpuNormal && (available&0x70))check(picked==(available&0x70));
    if(demand==PongCpuLatency && (available&0x80))check(picked==(available&0x80));
    cases++;
  }
  check(pongCpuPlacement(0xf,kinds,PongCpuLatency)==0xf);
  check(pongCpuPlacement(0x80,kinds,PongCpuBackground)==0x80);
  const uint64_t remapped[]={0x80,0xa,0x20};
  check(pongCpuPlacement(0xaa,remapped,PongCpuBackground)==0x80);
  check(pongCpuPlacement(0xaa,remapped,PongCpuNormal)==0xa);
  check(pongCpuPlacement(0xaa,remapped,PongCpuLatency)==0x20);
  const uint64_t unknown[]={0,0,0};check(pongCpuPlacement(0xaa,unknown,PongCpuLatency)==0xaa);
  check(pongCpuPlacement(255,kinds,99)==255);
  for(unsigned variant=0;variant<16;variant++)for(unsigned revision=0;revision<16;revision++) {
    check(pongCpuKind(0x410fd460u|(variant<<20)|revision)==0);
    check(pongCpuKind(0x410fd470u|(variant<<20)|revision)==1);
    check(pongCpuKind(0x410fd480u|(variant<<20)|revision)==2);
  }
  check(pongCpuKind(0)==-1);check(pongCpuKind(0x510fd470)==-1);
  std::printf("PASS %u available-mask/demand cases, busy-core fallback, non-assumed numbering and MIDR variants\n",cases);
}
