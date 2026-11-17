// SPDX-License-Identifier: MIT
#include "../drivers/PongAdreno/PongGpuVaPolicy.h"
#include "../drivers/PongAdreno/submit/NativeAdrenoClientWindow.h"
#include "../drivers/PongAdreno/ArmSmmu4K/ArmSmmu4KLive.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define check(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::_Exit(1);}}while(0)
static uint64_t translated(uint64_t *table,uint64_t physical,unsigned start,uint64_t address)
{
    uint64_t page=physical;
    for(unsigned level=start;level<3;level++) {
        uint64_t entry=table[(page-physical)/8+((address>>(39-9*level))&511)];
        check((entry&3)==3);page=entry&0x0000fffffffff000ULL;
    }
    uint64_t entry=table[(page-physical)/8+((address>>12)&511)];
    check((entry&3)==3);return (entry&0x0000fffffffff000ULL)|(address&4095);
}
int main()
{
    for(unsigned requested:{0u,32u,36u,40u,64u}) {
        const auto p=pongGpuVaPolicy(requested);
        check(p.bits==(requested==36?36u:32u));check(p.bytes==(requested==36?1ULL<<34:1ULL<<32));
        check(p.leaves==p.bytes/4096);check(p.table_pages==1+p.bytes/(1ULL<<30)+p.bytes/(1ULL<<21));
        uint64_t *tables=nullptr;check(!posix_memalign(reinterpret_cast<void **>(&tables),4096,p.table_pages*4096));
        std::memset(tables,0xa5,p.table_pages*4096);
        ArmSmmu4KLiveArena arena{{p.bits,40,0x900000000ULL,p.leaves},0,p.bytes,0,tables,p.table_pages,nullptr,0};
        ArmSmmu4KLiveState state{};
        auto short_arena=arena;short_arena.capacity_pages--;
        check(ArmSmmu4KLivePrepare(&short_arena,&state)==ArmSmmu4KCapacity && !state.prepared && tables[0]==0xa5a5a5a5a5a5a5a5ULL);
        check(ArmSmmu4KLivePrepare(&arena,&state)==ArmSmmu4KSuccess && state.prepared);
        check(state.tables.table_pages==p.table_pages && state.tables.start_level==1);
        ArmSmmu4KLiveResult audit{};check(ArmSmmu4KLiveAudit(&arena,&state,nullptr,0,&audit)==ArmSmmu4KSuccess);
        ArmSmmu4KContextConfig context{};context.lower=&arena.config;context.lower_tables=&state.tables;
        context.id0=1u<<30;context.id2=(1u<<12)|(2u<<8)|(2u<<4);context.asid=2;
        context.coherent_walk=1;context.polled_faults=1;context.continue_after_fault=1;context.shared_asid=1;context.ignore_top_byte=1;
        ArmSmmu4KContextImage image{};check(ArmSmmu4KEncodeContext(&context,&image)==ArmSmmu4KSuccess);
        check((image.tcr&63)==64-p.bits && (image.tcr2&0x60)==0x60);
        check(ArmSmmu4KValidateContextReadback(&context,&image)==ArmSmmu4KSuccess);
        auto wrong=image;wrong.tcr2^=0x20;check(ArmSmmu4KValidateContextReadback(&context,&wrong)==ArmSmmu4KStateMismatch);
        wrong=image;wrong.ttbr0^=0x1000;check(ArmSmmu4KValidateContextReadback(&context,&wrong)==ArmSmmu4KStateMismatch);
        auto wrongcfg=arena.config;auto wrongtables=state.tables;wrongcfg.input_bits=40;wrongtables.start_level=0;
        context.lower=&wrongcfg;context.lower_tables=&wrongtables;
        check(ArmSmmu4KEncodeContext(&context,&wrong)==ArmSmmu4KUnsupported);
        std::free(tables);
    }
    unsigned windows=0;
    for(unsigned bits:{32u,36u}) {
        const unsigned cap=nativeAdrenoClientCapacity(bits);check(cap==(bits==36?32u:16u));
        for(unsigned slot=0;slot<cap;slot++) {
            const auto w=nativeAdrenoClientWindow<32>(slot,bits);check(w.bytes && !(w.start&4095));
            check(w.start+w.bytes<=pongGpuVaPolicy(bits).bytes);
            if(slot<2)check(w.start==nativeAdrenoClientWindow<16>(slot).start && w.bytes==nativeAdrenoClientWindow<16>(slot).bytes);
            else if(bits==36)check(w.start>PongGpuHighProbeIova+8192 && w.bytes==(256ULL<<20));
            else check(w.start==nativeAdrenoClientWindow<16>(slot).start && w.bytes==(128ULL<<20));
            for(unsigned j=0;j<slot;j++) {auto other=nativeAdrenoClientWindow<32>(j,bits);check(w.start>=other.start+other.bytes || other.start>=w.start+w.bytes);}
            windows++;
        }
        check(nativeAdrenoClientWindow<32>(cap,bits).bytes==0);
        check(nativeAdrenoClientWindow<32>(~0u,bits).bytes==0);
    }
    check(!nativeAdrenoClientWindow<32>(2,40).bytes);
    alignas(4096) uint64_t small[16*512]{};
    ArmSmmu4KConfig cfg{36,40,0x900000000ULL,16};
    ArmSmmu4KRange ranges[]={{0x200000,0xa00000000ULL,4096,3,0,2},
        {PongGpuHighProbeIova,0xb00000000ULL,8192,3,0,2},
        {0x2effff000ULL,0xc00000000ULL,4096,3,0,2}};
    ArmSmmu4KResult built{};
    check(ArmSmmu4KBuild(&cfg,ranges,3,small,16,&built)==ArmSmmu4KSuccess);
    for(const auto &r:ranges)for(uint64_t offset=0;offset<r.bytes;offset+=4096)
        check(translated(small,cfg.table_physical,built.start_level,r.iova+offset)==r.physical+offset);
    uint32_t ib[4]{},launch[4]{};check(pongGpuHighProbePackets(PongGpuHighProbeIova,ib,launch));
    check(ib[1]==4096 && ib[2]==1 && ib[3]==PongGpuHighProbeValue);
    check(launch[1]==0 && launch[2]==1 && launch[3]==4);
    AdrenoPM4Packet packet{};size_t count=0;
    check(AdrenoPM4Plan(ib,4,&packet,1,&count)==AdrenoPM4Success && count==1 && packet.identifier==0x3d && packet.payload_words==3);
    check(AdrenoPM4Plan(launch,4,&packet,1,&count)==AdrenoPM4Success && packet.identifier==0x3f);
    check(!pongGpuHighProbePackets(0xfffff000,ib,launch));check(!pongGpuHighProbePackets(UINT64_MAX,ib,launch));
    std::printf("PASS32/36-bit native table layouts, exact TCR/TBI validation, %u disjoint windows, high-address translations and CP probe packet fields; hardware not executed\n",windows);
}
