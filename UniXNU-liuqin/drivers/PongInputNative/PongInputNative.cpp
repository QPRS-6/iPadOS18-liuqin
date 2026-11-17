/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Independent GENI/GPI transport. No dependency on pong_touch.c, its
 * bit-bang routines, firmware loader or recovery policies. */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>
#include "PongInputNativeBoard.h"
#include "PongNovatekProtocol.h"
#include "PongNativePenABI.h"
#include "../PongPower/PongPlatformResources.h"
#include "../PongAdreno/ArmSmmu4K/ArmSmmu4K.h"
#include "../PongAdreno/ArmSmmu4K/ArmSmmu4KProgram.h"
#include "../PongAdreno/ArmSmmu4K/ArmSmmu4KMmio.h"

extern "C" IOReturn armcpu_spi_attach(uint32_t,bool,void (*)(void *),void *,uint32_t *,uint64_t *);
/* Generic BSD input queues, not the previous touch transport/loader. */
extern "C" bool pong_touch_device_ready();
extern "C" void pong_touch_post(uint32_t,uint32_t,uint32_t,uint32_t);
extern "C" bool pong_native_pen_device_ready();
extern "C" void pong_native_pen_post(const pong_native_pen *);

namespace ponginput {
#if PONG_NATIVE_FIRMWARE_INCLUDED
#include "PongInputNativeFirmware.inc"
#else
static const uint8_t *const nativeFirmware=nullptr;
static constexpr unsigned nativeFirmwareBytes=0;
#endif
enum ProbeState : unsigned {Pass=1,Fail=2,Skip=3,Running=4};
struct Probe {unsigned sequence,state;const char *name,*reason;uint64_t a,b;};
static Probe probes[16];
/* Completed snapshots are printable before panic without MMIO or locks.
 * Only the worker writes records; IRQs publish counters/immutable snapshots. */
static void PongProbeRecord(unsigned slot,const char *name,ProbeState state,
                            const char *reason,uint64_t a=0,uint64_t b=0)
{
    if(slot>=16)return;
    Probe &p=probes[slot];
    __atomic_add_fetch(&p.sequence,1,__ATOMIC_ACQ_REL);
    __atomic_store_n(&p.name,name,__ATOMIC_RELAXED);
    __atomic_store_n(&p.reason,reason,__ATOMIC_RELAXED);
    __atomic_store_n(&p.a,a,__ATOMIC_RELAXED);__atomic_store_n(&p.b,b,__ATOMIC_RELAXED);
    __atomic_store_n(&p.state,unsigned(state),__ATOMIC_RELAXED);
    __atomic_add_fetch(&p.sequence,1,__ATOMIC_RELEASE);
    IOLog("PONG INPUT NATIVE: %s %s: %s a=%llx b=%llx\n",name,
          state==Pass?"PASS":state==Fail?"FAIL":state==Skip?"SKIP":"RUNNING",reason,a,b);
}
static void dump()
{
    for(unsigned i=0;i<16;i++) {
        Probe &p=probes[i];unsigned s=__atomic_load_n(&p.sequence,__ATOMIC_ACQUIRE);
        if(!s)continue;
        if(s&1){printf("PONG INPUT NATIVE: snapshot %u RUNNING (update interrupted)\n",i);continue;}
        const char *name=__atomic_load_n(&p.name,__ATOMIC_RELAXED);
        const char *reason=__atomic_load_n(&p.reason,__ATOMIC_RELAXED);
        unsigned state=__atomic_load_n(&p.state,__ATOMIC_RELAXED);
        uint64_t a=__atomic_load_n(&p.a,__ATOMIC_RELAXED),b=__atomic_load_n(&p.b,__ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(s!=__atomic_load_n(&p.sequence,__ATOMIC_RELAXED)) {
            printf("PONG INPUT NATIVE: snapshot %u SKIP (changed)\n",i);continue;
        }
        printf("PONG INPUT NATIVE: snapshot %u %s state=%u %s a=%llx b=%llx\n",
               i,name?name:"?",state,reason?reason:"?",a,b);
    }
}
struct Window {
    IOMemoryMap *keep=nullptr;uintptr_t base=0;unsigned bytes=0;
    bool map(uint64_t pa,unsigned size,bool photo=false) {
        if(photo)PongProbeRecord(13,"PHOTO G01",Running,"before range descriptor; no MMIO",pa,size);
        auto memory=IODeviceMemory::withRange(pa,size);
        if(!memory) {
            if(photo)PongProbeRecord(13,"PHOTO G01",Fail,"range descriptor unavailable; no MMIO",pa,size);
            return false;
        }
        if(photo)PongProbeRecord(13,"PHOTO G02",Running,"descriptor ready; before CPU map; no MMIO",pa,size);
        keep=memory->map(kIOMapAnywhere|kIOMapInhibitCache);
        if(photo)PongProbeRecord(13,"PHOTO G03",Running,"CPU map returned; before descriptor release; no MMIO",pa,keep?1u:0u);
        memory->release();
        if(!keep) {
            if(photo)PongProbeRecord(13,"PHOTO G03",Fail,"CPU mapping unavailable; no MMIO",pa,size);
            return false;
        }
        if(photo)PongProbeRecord(13,"PHOTO G04",Running,"before virtual address query; no MMIO",pa,size);
        base=keep->getVirtualAddress();bytes=size;
        if(photo)PongProbeRecord(13,"PHOTO G05",base?Pass:Fail,
            base?"CPU mapping ready; no register read/write":"CPU mapping has no virtual address",base,size);
        return base!=0;
    }
    uint32_t read(unsigned off) const {return *reinterpret_cast<volatile uint32_t *>(base+off);}
    void write(unsigned off,uint32_t value) const {
        __asm__ volatile("dsb sy":::"memory");
        *reinterpret_cast<volatile uint32_t *>(base+off)=value;
        __asm__ volatile("dsb sy":::"memory");
    }
};
static uint64_t nowUs(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
static void cacheSync(const void *p,unsigned bytes)
{
    uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
    uintptr_t line=4u<<((ctr>>16)&15),at=reinterpret_cast<uintptr_t>(p),end=at+bytes;
    for(at&=~(line-1);at<end;at+=line)__asm__ volatile("dc civac, %0"::"r"(at):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
struct Memory {
    IOBufferMemoryDescriptor *keep=nullptr;uint8_t *cpu=nullptr;uint64_t pa=0;unsigned bytes=0;
    bool allocate(unsigned size) {
        keep=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
            IOOptionBits(kIODirectionInOut)|IOOptionBits(kIOMemoryPhysicallyContiguous)|
            IOOptionBits(kIOMemoryMapperNone),size,4096);
        if(!keep)return false;
        IOByteCount contiguous=0;cpu=static_cast<uint8_t *>(keep->getBytesNoCopy());
        pa=keep->getPhysicalSegment(0,&contiguous,kIOMemoryMapperNone);bytes=size;
        if(!cpu || !pa || (pa&4095) || (uintptr_t(cpu)&4095) || contiguous<size ||
           pa+size<pa || pa+size>(UINT64_C(1)<<36))return false;
        bzero(cpu,size);cacheSync(cpu,size);return true;
    }
};
struct Controller {
    struct IrqFailure {
        uint32_t type=0,global=0,general=0,log=0,command=0;
        uint32_t tx=0,rx=0,event=0,txScratch=0,rxScratch=0;
        uint64_t count=0;
    } irqFailure;
    Window gcc,gpi,se,tlmm,smmu,wrapper;
    Memory dma,tables;
    unsigned mode=0,instance=0,bank=0,slots[2]={~0u,~0u},oldRoutes[2]={0,0};
    unsigned txWrite=0,rxWrite=0,eventRead=0,eventCredit=EventEntries-1;
    unsigned channelAck[2]={0,0},eventAck=0,eventNotifications=0,error=0;
    unsigned transferSequence=0,eventLogCount=0;
    bool quietTransfers=false;
    uint8_t protocolTx[4097]={};
    FirmwarePlan firmwarePlan;
    bool touchPublished=false,penPublished=false;
    bool reportReadyOwned=false;uint32_t reportOldCfg=0,reportWantedCfg=0;
    bool reportPageSelected=false;
    uint32_t lastReportAck=0;
    NativeContact lastContacts[10];unsigned contactMask=0;
    NativePen lastPen;
    unsigned penHoverSamples=0,penTipSamples=0,penOutSamples=0,penIdSamples=0;
    unsigned penRejected[6]={},penRawPeak=0,penAcceptedPeak=0;
    unsigned penRawHoverLogs=0,penRawTipLogs=0,penRawRejectLogs=0,penRawTransitionLogs=0;
    bool penTraceRange=false,penTraceTouch=false;
    unsigned irqFailureState=0,activeCommand=~0u;
    bool irqFailureReported=false;
    uint32_t seClockConfig=0;
    bool seClockConfigured=false;
    uint64_t irqCount=0;
    bool resourcesRetained=false,streamsChanged=false,exids=false,gpiIrqAttached=false,started[2]={false,false};
    ArmSmmu4KConfig tableConfig={};ArmSmmu4KResult tableResult={};
    ArmSmmu4KRange range={};ArmSmmu4KContextConfig context={};
    ArmSmmu4KMmio smmuMapping={};ArmSmmu4KIo smmuIo={},smmuRawIo={};

    unsigned c(unsigned off) const{return control(instance,off);}
    bool waitClear(const Window &w,unsigned off,uint32_t mask,unsigned timeout) {
        uint64_t end=nowUs()+timeout;
        for(unsigned i=0;i<timeout;i++) {
            if(!(w.read(off)&mask))return true;
            if(nowUs()>=end)break;IODelay(1);
        }
        return false;
    }
    bool clocks() {
        PongProbeRecord(0,"wrapper clocks",Running,"enable only held wrapper votes");
        if(!gcc.map(GccPa,0x100000)) {
            PongProbeRecord(0,"wrapper clocks",Fail,"GCC mapping failed");return false;
        }
        gcc.write(ClockVote,gcc.read(ClockVote)|WrapperClockMask);
        uint32_t votes=gcc.read(ClockVote);
        if((votes&WrapperClockMask)!=WrapperClockMask) {
            PongProbeRecord(0,"wrapper clocks",Fail,"wrapper vote readback missing requested bits",votes,WrapperClockMask);
            return false;
        }
        unsigned hwcgBranches=0;
        for(const WrapperClockBranch &branch:WrapperClocks) {
            unsigned polls=0;uint64_t end=nowUs()+10000;
            uint32_t status=gcc.read(branch.halt);
            while(!wrapperClockReady(branch,votes,status) && polls<10000 && nowUs()<end) {
                IODelay(1);++polls;votes=gcc.read(ClockVote);status=gcc.read(branch.halt);
            }
            const bool hwcg=(status&branch.hwcg)!=0;
            IOLog("PONG INPUT NATIVE: wrapper branch off=%x vote=%08x cbcr=%08x hwcg=%u polls=%u\n",
                branch.halt,votes,status,unsigned(hwcg),polls);
            if(!wrapperClockReady(branch,votes,status)) {
                PongProbeRecord(0,"wrapper clocks",Fail,"vote missing or ungated branch remains halted",branch.halt,status);
                return false;
            }
            if(hwcg)hwcgBranches|=branch.vote;
        }
        /* clk-branch.c skips HALT polling on a HWCG branch: an idle clock may
         * legitimately halt. Do not clear HWCG on the shared wrapper, and do
         * not claim physical activity from an enable vote. Real DMA is later. */
        PongProbeRecord(0,"wrapper clocks",Pass,"votes held; HWCG-aware prerequisites accepted, not DMA proof",
                        gcc.read(ClockVote),hwcgBranches);
        return true;
    }
    bool inspectSharedSe() {
        PongProbeRecord(2,"shared SE modes",Running,
            "inspect existing voted engines only; no clock/SE writes or takeover");
        unsigned checked=0;
        for(unsigned n=0;n<=6;n++) {
            const uint32_t bit=1u<<(10+n),votes=gcc.read(ClockVote);
            if(n==SeId || !(votes&bit))continue;
            const unsigned halt=SeBranchHalts[n];
            PongProbeRecord(14,"PHOTO E00",Running,
                "before existing SE branch-status read: a=GCC PA b=engine",GccPa+halt,n);
            __asm__ volatile("dsb sy":::"memory");
            const uint32_t cbcr=gcc.read(halt);
            __asm__ volatile("dsb sy":::"memory");
            PongProbeRecord(14,"PHOTO E00",Pass,"SE branch-status returned: a=GCC PA b=CBCR",GccPa+halt,cbcr);
            IOLog("PONG INPUT NATIVE: shared SE engine=%u vote=%08x cbcr=%08x halt=%x\n",n,votes,cbcr,halt);
            if((cbcr&(1u<<31)) || !(gcc.read(ClockVote)&bit)) {
                PongProbeRecord(2,"shared SE modes",Skip,
                    "SE branch halted or vote lost; dependent SE MMIO NOT attempted",n,cbcr);
                return false;
            }
            const uint64_t pa=wrapperSePa(n);
            PongProbeRecord(14,"PHOTO E01",Running,"before existing SE CPU map; no SE writes",pa,n);
            Window other;
            if(!other.map(pa,SeRegisterBytes,true)) {
                PongProbeRecord(2,"shared SE modes",Fail,"existing SE CPU mapping failed; no mode read",pa,n);
                return false;
            }
            if(!(gcc.read(ClockVote)&bit)) {
                PongProbeRecord(2,"shared SE modes",Skip,"SE vote lost after map; no mode read",pa,n);return false;
            }
            PongProbeRecord(14,"PHOTO E02",Running,
                "before ONE existing SE DMA-mode read; no command or pin write",pa+SeDmaModeOffset,n);
            __asm__ volatile("dsb sy":::"memory");
            const uint32_t dmaMode=other.read(SeDmaModeOffset);
            __asm__ volatile("dsb sy":::"memory");
            ++checked;
            PongProbeRecord(14,"PHOTO E03",Pass,"existing SE mode returned: a=PA b=value",pa+SeDmaModeOffset,dmaMode);
            if(dmaMode&1) {
                PongProbeRecord(2,"shared SE modes",Skip,"other engine configured for DMA; no shared-route takeover",n,dmaMode);
                return false;
            }
        }
        PongProbeRecord(2,"shared SE modes",Pass,
            "voted other engines observed without DMA; no ownership lease claimed",checked,gcc.read(ClockVote));
        return true;
    }
    bool idleMasters() {
        PongProbeRecord(1,"GPI ownership",Running,"inspect APPS instances; reserved instance 0 untouched");
        if(!gpi.map(GpiPa,GpiBytes)) {
            PongProbeRecord(1,"GPI ownership",Fail,"GPI mapping failed");return false;
        }
        for(unsigned n=1;n<=6;n++) {
            uint32_t tx=gpi.read(channel(n,0)),rx=gpi.read(channel(n,1)),ev=gpi.read(event(n));
            if(((tx|rx|ev)>>20)&15) {
                PongProbeRecord(1,"GPI ownership",Skip,"allocated APPS GPI context; shared DMA not taken",n,
                                uint64_t(tx)|(uint64_t(rx)<<32));return false;
            }
            if(gpi.read(control(n,0x23080))) {
                PongProbeRecord(1,"GPI ownership",Skip,"pending firmware GPI interrupt",n);return false;
            }
            if(!instance)instance=n;
        }
        /* Wrapper data SID is shared. An enable vote is not enough to read
         * another SE: retain branch prerequisites and per-access snapshots. */
        if(!inspectSharedSe())return false;
        PongProbeRecord(1,"GPI ownership",Pass,"APPS GPI and shared SE DMA available",instance);
        return instance!=0;
    }
    bool prepareSe4Clock() {
        PongProbeRecord(2,"SE4 clock",Running,"inspect clock ownership before target-only configuration");
        const unsigned offsets[]={ClockVote,SeHalt,SeRcg,SeRcg+4};
        uint32_t before[4]={};
        for(unsigned k=0;k<4;k++) {
            unsigned off=offsets[k];
            if(!gcc.base || gcc.bytes<4 || off>gcc.bytes-4) {
                PongProbeRecord(2,"SE4 clock",Skip,"GCC range unavailable; no target access",off);return false;
            }
            PongProbeRecord(14,"PHOTO C01",Running,"before SE4 clock-register read: a=GCC PA b=kind",GccPa+off,k);
            __asm__ volatile("dsb sy":::"memory");
            before[k]=gcc.read(off);
            __asm__ volatile("dsb sy":::"memory");
            PongProbeRecord(14,"PHOTO C02",Pass,"clock-register read returned: a=GCC PA b=value",GccPa+off,before[k]);
        }
        IOLog("PONG INPUT NATIVE: SE4 clock before vote=%08x CBCR=%08x CMD=%08x CFG=%08x\n",
            before[0],before[1],before[2],before[3]);
        if(before[2]&1) {
            PongProbeRecord(2,"SE4 clock",Skip,"another RCG update pending; preserve clock state",before[2]);return false;
        }
        if(before[0]&SeClockBit) {
            if((before[3]&SeRcgRateMask)!=1) {
                PongProbeRecord(2,"SE4 clock",Skip,"voted SE4 has different clock config; no live reparent",before[3]);return false;
            }
        } else {
            if(!(before[1]&(1u<<31))) {
                PongProbeRecord(2,"SE4 clock",Skip,"SE4 running without our vote; no clock takeover",before[1]);return false;
            }
            const uint32_t config=(before[3]&~SeRcgControlMask)|SeRcgTcxoConfig;
            PongProbeRecord(14,"PHOTO C03",Running,"before idle SE4 RCG config write only",GccPa+SeRcg+4,config);
            gcc.write(SeRcg+4,config);seClockConfigured=true;
            PongProbeRecord(14,"PHOTO C04",Running,"before SE4 RCG UPDATE; no SE command write",GccPa+SeRcg,before[2]|1u);
            gcc.write(SeRcg,before[2]|1u);
            if(!waitClear(gcc,SeRcg,1,10000)) {
                PongProbeRecord(2,"SE4 clock",Fail,"target RCG UPDATE timed out",gcc.read(SeRcg));return false;
            }
            PongProbeRecord(14,"PHOTO C05",Running,"before target SE4 vote only; other votes retained",GccPa+ClockVote,SeClockBit);
            gcc.write(ClockVote,gcc.read(ClockVote)|SeClockBit);
        }
        PongProbeRecord(14,"PHOTO C06",Running,"wait for actual SE4 branch; no SE register access yet",GccPa+SeHalt);
        if(!(gcc.read(ClockVote)&SeClockBit) || !waitClear(gcc,SeHalt,1u<<31,10000)) {
            PongProbeRecord(2,"SE4 clock",Fail,"target vote missing or branch remains halted",gcc.read(ClockVote),gcc.read(SeHalt));return false;
        }
        seClockConfig=gcc.read(SeRcg+4);
        if((seClockConfig&SeRcgRateMask)!=1) {
            PongProbeRecord(2,"SE4 clock",Fail,"target RCG readback differs from TCXO/div1",seClockConfig);return false;
        }
        PongProbeRecord(2,"SE4 clock",Pass,"TCXO/div1 config and branch ready; no SPI/DMA attempted",gcc.read(ClockVote),seClockConfig);
        return true;
    }
    bool serialEngine() {
        if(!prepareSe4Clock())return false;
        PongProbeRecord(2,"GENI SE4",Running,"configure hardware SPI after clock prerequisites");
        if(!se.map(SePa,0x4000) || !wrapper.map(WrapperPa,0x2000) || !tlmm.map(TlmmPa,TlmmBytes)) {
            PongProbeRecord(2,"GENI SE4",Fail,"SE/wrapper/TLMM mapping failed");return false;
        }
        uint32_t fw=se.read(0x68),status=se.read(0x40),version=wrapper.read(4);
        if(((fw>>8)&255)!=1 || ((version>>28)&15)<2 || (status&0x1001)) {
            PongProbeRecord(2,"GENI SE4",Skip,"wrong protocol/version or active firmware command",fw,status);return false;
        }
        if(!(se.read(0x64)&1)) {
            PongProbeRecord(2,"GENI SE4",Skip,"not the expected GPI-only interface",se.read(0x64));return false;
        }
        for(unsigned pin=16;pin<=19;pin++) {
            PongProbeRecord(14,"PHOTO P01",Running,"before owned SE4 native QUP pin mux; no GPIO SPI",TlmmPa+pin*0x1000,pin);
            unsigned off=pin*0x1000;
            if(tlmm.bytes<4 || off>tlmm.bytes-4) {
                PongProbeRecord(2,"GENI SE4",Skip,"QUP pin ctl outside mapped TLMM; no pin access",pin,off);return false;
            }
            const uint32_t before=tlmm.read(off),wanted=(before&~0x3ffu)|0x84u;
            tlmm.write(off,wanted); // native QUP4, no GPIO output
            const uint32_t after=tlmm.read(off);
            IOLog("PONG INPUT NATIVE: QUP4 pin=%u PA=%llx before=%08x wanted=%08x readback=%08x\n",
                pin,TlmmPa+off,before,wanted,after);
            if((after&0x3ffu)!=0x84u) {
                PongProbeRecord(2,"GENI SE4",Fail,"QUP4 pin mux/drive readback mismatch; no SE/GPI start",pin,after);return false;
            }
        }
        PongProbeRecord(14,"PHOTO P02",Running,"before SE4 GPI event/mode preparation; no panel reset",SePa);
        se.write(0xe18,0);se.write(0xe1c,0);
        se.write(0x614,0);se.write(0x644,0);
        se.write(0x618,~0u);se.write(0x648,~0u);
        se.write(0x28,se.read(0x28)|0x7f);se.write(0xe30,se.read(0xe30)|0xf);
        se.write(0x24,0x7f);se.write(0x20,1);
        se.write(0x224,0);se.write(0x230,0);se.write(0x22c,0);
        se.write(0x24c,se.read(0x24c)&~15u);se.write(0x250,0);
        se.write(0x25c,se.read(0x25c)&~2u);se.write(0x2bc,0);
        se.write(0x258,1);se.write(0xe18,15);
        PongProbeRecord(2,"GENI SE4",Pass,"GPI mode; CS0; requested SPI 9.6MHz",fw,version);
        return true;
    }
    bool smmuPhotoRead(unsigned off,uint64_t kind,uint32_t &value) {
        if(!smmu.base || (off&3) || smmu.bytes<4 || off>smmu.bytes-4) {
            PongProbeRecord(4,"SMMU read-only",Skip,"register outside mapped range; no MMIO",off,kind);return false;
        }
        PongProbeRecord(14,"PHOTO A02",Running,"before APPS SMMU read: a=PA b=kind/index",SmmuPa+off,kind);
        __asm__ volatile("dsb sy":::"memory");
        value=smmu.read(off);
        __asm__ volatile("dsb sy":::"memory");
        PongProbeRecord(14,"PHOTO A03",Pass,"SMMU load returned: a=PA b=value; no mapping writes",SmmuPa+off,value);
        return true;
    }
    bool inspectSmmuRoutes() {
        PongProbeRecord(4,"SMMU read-only",Running,"geometry and target stream observations only; no leases or writes");
        PongProbeRecord(14,"PHOTO A01",Running,"before APPS SMMU CPU map; no DMA or page tables",SmmuPa,SmmuBytes);
        if(!smmu.map(SmmuPa,SmmuBytes,true)) {
            PongProbeRecord(4,"SMMU read-only",Fail,"CPU mapping unavailable; no register reads");return false;
        }
        const unsigned offsets[]={0x20,0x24,0x28,0,0x48};
        uint32_t ids[5]={};
        for(unsigned k=0;k<5;k++)if(!smmuPhotoRead(offsets[k],k,ids[k]))return false;
        const uint32_t id0=ids[0],id1=ids[1];
        const unsigned groups=id0&255,banks=id1&255,pages=1u<<(((id1>>28)&7)+1);
        IOLog("PONG INPUT NATIVE: APPS SMMU ID0=%08x ID1=%08x ID2=%08x SCR0=%08x GFSR=%08x groups=%u banks=%u pages=%u\n",
            ids[0],ids[1],ids[2],ids[3],ids[4],groups,banks,pages);
        if((ids[3]&1) || !(id0&(1u<<27)) || !groups || groups>128 || !banks || banks>128 ||
           (id1&(1u<<31)) || (uint64_t(pages)+banks)*4096>SmmuBytes || ids[4]) {
            PongProbeRecord(4,"SMMU read-only",Skip,"unsupported geometry/client policy or global fault; no route scan/reset",id0,id1);
            return false;
        }
        const bool ex=(id0&(1u<<8))!=0;
        const uint16_t sids[]={RingSid,DataSid};
        unsigned matches[2]={},reads=5;
        for(unsigned i=0;i<groups;i++) {
            uint32_t smr=0;
            if(!smmuPhotoRead(0x800+4*i,(uint64_t(i)<<32)|0x100u,smr))return false;
            ++reads;
            if(!ex && !(smr&(1u<<31)))continue;
            const unsigned mask=(smr>>16)&(ex?0xffffu:0x7fffu);
            const bool relevant[]={!((smr^sids[0])&~mask&0xffffu),!((smr^sids[1])&~mask&0xffffu)};
            if(!relevant[0] && !relevant[1])continue;
            uint32_t route=0;
            if(!smmuPhotoRead(0xc00+4*i,(uint64_t(i)<<32)|0x101u,route))return false;
            ++reads;
            const bool valid=ex?(route&(1u<<10)):(smr&(1u<<31));
            IOLog("PONG INPUT NATIVE: SMMU target candidate slot=%u SMR=%08x S2CR=%08x valid=%u mask=%x type=%u bank=%u\n",
                i,smr,route,unsigned(valid),mask,(route>>16)&3,route&255);
            if(!valid)continue;
            for(unsigned k=0;k<2;k++)if(relevant[k]) {
                ++matches[k];
                if(mask || matches[k]>1 || ((route>>16)&3)==3 ||
                   (((route>>16)&3)==0 && (route&255)>=banks)) {
                    PongProbeRecord(4,"SMMU read-only",Skip,"masked/duplicate/invalid target route; no takeover",smr,route);return false;
                }
                IOLog("PONG INPUT NATIVE: SMMU SID=%x slot=%u route=%08x observed only; no lease\n",sids[k],i,route);
            }
        }
        IOLog("PONG INPUT NATIVE: SMMU route observations RingSID=%x matches=%u DataSID=%x matches=%u reads=%u EXIDS=%u\n",
            RingSid,matches[0],DataSid,matches[1],reads,unsigned(ex));
        PongProbeRecord(4,"SMMU read-only",Pass,"route observations completed; no bank/page-table/stream writes or leases",
                        matches[0],matches[1]);
        return true;
    }
    bool inspectSmmuPlan() {
        PongProbeRecord(4,"SMMU plan-only",Running,"observe free slots/banks/ASIDs; no allocation, locks or writes");
        PongProbeRecord(14,"PHOTO B01",Running,"before SMMU CPU map for read-only planning",SmmuPa,SmmuBytes);
        if(!smmu.map(SmmuPa,SmmuBytes,true)) {
            PongProbeRecord(4,"SMMU plan-only",Fail,"CPU mapping failed; no plan reads");return false;
        }
        const unsigned offsets[]={0x20,0x24,0x28,0,0x48};
        uint32_t ids[5]={};
        for(unsigned k=0;k<5;k++)if(!smmuPhotoRead(offsets[k],k,ids[k]))return false;
        const uint32_t id0=ids[0],id1=ids[1];
        const unsigned groups=id0&255,banks=id1&255,pages=1u<<(((id1>>28)&7)+1),s2banks=(id1>>16)&255;
        IOLog("PONG INPUT NATIVE: SMMU plan geometry ID0=%08x ID1=%08x ID2=%08x SCR0=%08x GFSR=%08x groups=%u banks=%u pages=%u S2banks=%u\n",
            ids[0],ids[1],ids[2],ids[3],ids[4],groups,banks,pages,s2banks);
        if((ids[3]&1) || !(id0&(1u<<27)) || !groups || groups>128 || !banks || banks>128 ||
           s2banks>banks || (id1&(1u<<31)) || (uint64_t(pages)+banks)*4096>SmmuBytes || ids[4]) {
            PongProbeRecord(4,"SMMU plan-only",Skip,"unsupported geometry/policy/fault; no plan",id0,id1);return false;
        }
        const bool ex=(id0&(1u<<8))!=0;
        bool referenced[128]={},eligible[128]={},asidUsed[129]={};
        uint32_t controls[128]={};
        unsigned freeSlots[128],freeCount=0,reads=5,active=0,eligibleCount=0;
        for(unsigned i=0;i<groups;i++) {
            uint32_t smr=0,route=0;
            if(!smmuPhotoRead(0x800+4*i,(uint64_t(i)<<32)|0x100u,smr) ||
               !smmuPhotoRead(0xc00+4*i,(uint64_t(i)<<32)|0x101u,route))return false;
            reads+=2;
            const bool valid=ex?(route&(1u<<10)):(smr&(1u<<31));
            if(!valid){freeSlots[freeCount++]=i;continue;}
            const unsigned type=(route>>16)&3,cb=route&255,mask=(smr>>16)&(ex?0xffffu:0x7fffu);
            if(type==3 || (type==0 && cb>=banks)) {
                PongProbeRecord(4,"SMMU plan-only",Skip,"uninterpretable active route; cannot account ownership",smr,route);return false;
            }
            if(type==0)referenced[cb]=true;
            if(!((smr^RingSid)&~mask&0xffffu) || !((smr^DataSid)&~mask&0xffffu)) {
                PongProbeRecord(4,"SMMU plan-only",Skip,"target route now active; no claim or replacement",smr,route);return false;
            }
        }
        if(freeCount<2) {
            PongProbeRecord(4,"SMMU plan-only",Skip,"fewer than two invalid stream slots",freeCount);return false;
        }
        for(unsigned n=0;n<banks;n++) {
            const unsigned cb=(pages+n)*4096;
            if(!smmuPhotoRead(cb,(uint64_t(n)<<32)|0x200u,controls[n]))return false;
            ++reads;
            uint32_t hi0=0,hi1=0,fault=0;
            if(controls[n]&1) {
                ++active;
                if(!smmuPhotoRead(cb+0x24,(uint64_t(n)<<32)|0x201u,hi0) ||
                   !smmuPhotoRead(cb+0x2c,(uint64_t(n)<<32)|0x202u,hi1))return false;
                reads+=2;
                const unsigned a0=hi0>>16,a1=hi1>>16;
                if(a0<=banks)asidUsed[a0]=true;
                if(a1<=banks)asidUsed[a1]=true;
                IOLog("PONG INPUT NATIVE: SMMU bank=%u active SCTLR=%08x referenced=%u ASID0=%u ASID1=%u\n",
                    n,controls[n],unsigned(referenced[n]),a0,a1);
            } else if(!referenced[n] && n>=s2banks) {
                if(!smmuPhotoRead(cb+0x58,(uint64_t(n)<<32)|0x203u,fault))return false;
                ++reads;
                if(!fault){eligible[n]=true;++eligibleCount;}
                IOLog("PONG INPUT NATIVE: SMMU bank=%u disabled SCTLR=%08x FSR=%08x candidate=%u; no clearing\n",
                    n,controls[n],fault,unsigned(eligible[n]));
            } else {
                IOLog("PONG INPUT NATIVE: SMMU bank=%u disabled but referenced/reserved; no candidate\n",n);
            }
        }
        unsigned candidateBank=~0u,candidateAsid=0;
        for(unsigned n=banks;n-->s2banks;)if(eligible[n] && !asidUsed[n+1]) {
            candidateBank=n;candidateAsid=n+1;break;
        }
        uint32_t global=0;
        if(!smmuPhotoRead(0x48,5,global))return false;
        ++reads;
        if(global || candidateBank==~0u) {
            PongProbeRecord(4,"SMMU plan-only",Skip,"global fault or no observed eligible bank/ASID",global,eligibleCount);return false;
        }
        IOLog("PONG INPUT NATIVE: SMMU PLAN ringSlot=%u dataSlot=%u bank=%u asid=%u freeSlots=%u activeBanks=%u eligibleBanks=%u reads=%u OBSERVED ONLY NOT RESERVED\n",
            freeSlots[freeCount-1],freeSlots[freeCount-2],candidateBank,candidateAsid,freeCount,active,eligibleCount,reads);
        PongProbeRecord(4,"SMMU plan-only",Pass,"candidate observations complete; no ownership, leases or register writes",
            uint64_t(freeSlots[freeCount-1])|(uint64_t(freeSlots[freeCount-2])<<32),uint64_t(candidateBank)|(uint64_t(candidateAsid)<<32));
        return true;
    }
    static int smmuRead32Forward(void *cookie,size_t off,uint32_t *value) {
        Controller &d=*static_cast<Controller *>(cookie);
        return d.smmuRawIo.read32(d.smmuRawIo.cookie,off,value);
    }
    static int smmuRead64Forward(void *cookie,size_t off,uint64_t *value) {
        Controller &d=*static_cast<Controller *>(cookie);
        return d.smmuRawIo.read64(d.smmuRawIo.cookie,off,value);
    }
    static int smmuWrite32Trace(void *cookie,size_t off,uint32_t value) {
        Controller &d=*static_cast<Controller *>(cookie);
        PongProbeRecord(14,"PHOTO W01",Running,"before owned SMMU store32; a=PA b=value",SmmuPa+off,value);
        int result=d.smmuRawIo.write32(d.smmuRawIo.cookie,off,value);
        PongProbeRecord(14,"PHOTO W02",result?Fail:Pass,"store callback returned; hardware readback still required",SmmuPa+off,uint32_t(result));
        return result;
    }
    static int smmuWrite64Trace(void *cookie,size_t off,uint64_t value) {
        Controller &d=*static_cast<Controller *>(cookie);
        PongProbeRecord(14,"PHOTO W01",Running,"before owned SMMU store64; a=PA b=value",SmmuPa+off,value);
        int result=d.smmuRawIo.write64(d.smmuRawIo.cookie,off,value);
        PongProbeRecord(14,"PHOTO W02",result?Fail:Pass,"store64 callback returned; hardware readback still required",SmmuPa+off,uint32_t(result));
        return result;
    }
    static int smmuBarrierForward(void *cookie) {
        Controller &d=*static_cast<Controller *>(cookie);
        return d.smmuRawIo.barrier(d.smmuRawIo.cookie);
    }
    void ownedStreamWrite(unsigned off,uint32_t value) {
        PongProbeRecord(14,"PHOTO W01",Running,"before owned target-stream store; a=PA b=value",SmmuPa+off,value);
        smmu.write(off,value);
    }
    bool mappingMastersQuiescent() {
        if(!gpi.base || !se.base)return false;
        for(unsigned n=1;n<=6;n++) {
            uint32_t state=gpi.read(channel(n,0))|gpi.read(channel(n,1))|gpi.read(event(n));
            if(((state>>20)&15) || gpi.read(control(n,0x23080))) {
                PongProbeRecord(4,"apps SMMU",Skip,"GPI allocation/IRQ changed before mapping",n,state);return false;
            }
        }
        if(se.read(0x40)&0x1001) {
            PongProbeRecord(4,"apps SMMU",Skip,"target SE command active before mapping");return false;
        }
        return inspectSharedSe();
    }
    bool translation() {
        PongProbeRecord(3,"DMA ownership",Running,"allocate real contiguous rings, data and tables");
        PongProbeRecord(14,"PHOTO M01",Running,"before real DMA/table allocation; no DMA started",DmaBytes,TableBytes);
        if(!dma.allocate(DmaBytes) || !tables.allocate(TableBytes)) {
            PongProbeRecord(3,"DMA ownership",Fail,"physical allocation/contiguity failed");return false;
        }
        PongProbeRecord(3,"DMA ownership",Pass,"buffers retained until proven DMA drain",dma.pa,tables.pa);
        PongProbeRecord(4,"apps SMMU",Running,"lease exact GPI and SE streams; preserve global policy");
        if(!smmu.map(SmmuPa,SmmuBytes)) {
            PongProbeRecord(4,"apps SMMU",Fail,"SMMU mapping failed");return false;
        }
        if(!pong_smmu_lease_lock()) {
            PongProbeRecord(4,"apps SMMU",Skip,"platform lease lock unavailable");return false;
        }
        bool ok=translationLocked();pong_smmu_lease_unlock();return ok;
    }
    void mappingReadbackDiagnostic(const ArmSmmu4KContextImage &expected,const ArmSmmu4KProgramResult &result) {
        const ArmSmmu4KContextImage &actual=result.readback;
        IOLog("PONG INPUT NATIVE: SMMU retained readback fields after program failure; may be incomplete on I/O failure; cleanup ran, NOT current state proof ID0=%08x ID1=%08x ID2=%08x\n",
            result.id0,result.id1,result.id2);
        IOLog("PONG INPUT NATIVE: READBACK CBAR expected=%08x actual=%08x CBA2R expected=%08x actual=%08x\n",
            expected.cbar,actual.cbar,expected.cba2r,actual.cba2r);
        IOLog("PONG INPUT NATIVE: READBACK TCR expected=%08x actual=%08x TCR2 expected=%08x actual=%08x\n",
            expected.tcr,actual.tcr,expected.tcr2,actual.tcr2);
        IOLog("PONG INPUT NATIVE: READBACK TTBR0 expected=%016llx actual=%016llx TTBR1 expected=%016llx actual=%016llx\n",
            expected.ttbr0,actual.ttbr0,expected.ttbr1,actual.ttbr1);
        IOLog("PONG INPUT NATIVE: READBACK MAIR0 expected=%08x actual=%08x MAIR1 expected=%08x actual=%08x\n",
            expected.mair0,actual.mair0,expected.mair1,actual.mair1);
        IOLog("PONG INPUT NATIVE: READBACK SCTLR captured=%08x expected-disabled=00000000 expected-active=%08x reserved=%08x\n",
            actual.sctlr,expected.sctlr,actual.reserved_zero);
        const unsigned mask=(unsigned(expected.cbar!=actual.cbar)<<0)|(unsigned(expected.cba2r!=actual.cba2r)<<1)|
            (unsigned(expected.tcr!=actual.tcr)<<2)|(unsigned(expected.tcr2!=actual.tcr2)<<3)|
            (unsigned(expected.ttbr0!=actual.ttbr0)<<4)|(unsigned(expected.ttbr1!=actual.ttbr1)<<5)|
            (unsigned(expected.mair0!=actual.mair0)<<6)|(unsigned(expected.mair1!=actual.mair1)<<7)|
            (unsigned(actual.reserved_zero!=0)<<8);
        PongProbeRecord(10,"SMMU readback detail",Fail,
            "retained diff bits CBAR0 CBA2R1 TCR_2 TCR2_3 TTBR0_4 TTBR1_5 MAIR0_6 MAIR1_7 RESERVED8; SCTLR separate",
            mask,actual.sctlr);
    }
    bool translationLocked() {
        uint32_t id0=smmu.read(0x20),id1=smmu.read(0x24),id2=smmu.read(0x28),scr=smmu.read(0);
        unsigned groups=id0&255,banks=id1&255,pages=1u<<(((id1>>28)&7)+1);
        if((scr&1) || !(id0&(1u<<27)) || !groups || groups>128 || !banks || banks>128 ||
           (id1&(1u<<31)) || 2u*pages*4096>smmu.bytes || (pages+banks)*4096>smmu.bytes || smmu.read(0x48)) {
            PongProbeRecord(4,"apps SMMU",Skip,"unsupported/global faulted SMMU; no global reset",id0,id1);return false;
        }
        const bool ex=(id0&(1u<<8))!=0;exids=ex;
        bool used[128]={},active[128]={},asidUsed[129]={};unsigned freeSlots[128],freeCount=0;
        const uint16_t sids[]={RingSid,DataSid};
        for(unsigned i=0;i<groups;i++) {
            uint32_t smr=smmu.read(0x800+4*i),route=smmu.read(0xc00+4*i);
            bool valid=ex?(route&(1u<<10)):(smr&(1u<<31));
            if(valid && (((route>>16)&3)==3 || (!(route&(3u<<16)) && (route&255)>=banks))) {
                PongProbeRecord(4,"apps SMMU",Skip,"cannot account active route ownership",smr,route);return false;
            }
            if(valid && !(route&(3u<<16)))used[route&255]=true;
            if(!valid){freeSlots[freeCount++]=i;continue;}
            unsigned mask=(smr>>16)&(ex?0xffffu:0x7fffu);
            for(unsigned k=0;k<2;k++)if(!((smr^sids[k])&~mask&0xffffu)) {
                PongProbeRecord(4,"apps SMMU",Skip,"target route already active outside this lease; no replacement",smr,route);return false;
            }
        }
        for(unsigned k=0;k<2;k++)if(slots[k]==~0u) {
            if(!freeCount){PongProbeRecord(4,"apps SMMU",Skip,"no free exact-match slot");return false;}
            slots[k]=freeSlots[--freeCount];oldRoutes[k]=smmu.read(0xc00+4*slots[k]);
        }
        for(unsigned n=0;n<banks;n++) {
            const unsigned cb=(pages+n)*4096;
            active[n]=(smmu.read(cb)&1)!=0;
            if(!active[n])continue;
            if(!(smmu.read(0x1800+4*n)&1)) {
                PongProbeRecord(4,"apps SMMU",Skip,"active non-VA64 bank; ASID ownership not accounted",n);return false;
            }
            unsigned a0=smmu.read(cb+0x24)>>16,a1=smmu.read(cb+0x2c)>>16;
            if(a0<=banks)asidUsed[a0]=true;
            if(a1<=banks)asidUsed[a1]=true;
        }
        unsigned asid=0;
        for(unsigned n=banks;n-->(id1>>16&255);) {
            if(used[n] || active[n] || asidUsed[n+1] || smmu.read((pages+n)*4096+0x58))continue;
            bank=n;asid=n+1;break;
        }
        if(!asid){PongProbeRecord(4,"apps SMMU",Skip,"no disabled unreferenced bank/ASID");return false;}
        tableConfig={32,36,tables.pa,DmaBytes/4096};
        range={DmaIova,dma.pa,DmaBytes,ArmSmmu4KRead|ArmSmmu4KWrite,0,2};
        ArmSmmu4KStatus r=ArmSmmu4KBuild(&tableConfig,&range,1,
            reinterpret_cast<uint64_t *>(tables.cpu),TableBytes/4096,&tableResult);
        if(r){PongProbeRecord(4,"apps SMMU",Fail,"native 4K table build",r);return false;}
        cacheSync(tables.cpu,TableBytes);
        context.lower=&tableConfig;context.lower_tables=&tableResult;context.asid=asid;
        context.polled_faults=1;context.coherent_walk=0;context.id0=id0;context.id2=id2;
        /* 43.txt: all configuration fields matched except TCR2 TBI0/1=0x60.
         * Explicitly request the observed TBI policy, not a broad readback mask.
         * Encoder refuses TBI with upper/wider regions; admitted untagged
         * low32-bit IOVAs are unchanged (DmaIova10000000). */
        context.ignore_top_byte=1;
        /* 44.txt: TCR2 and all other config fields now match; SCTLR only
         * differs in ASIDPNE1000. Explicit shared-ASID policy clears that bit.
         * The locked allocator above leases a globally unused ASID and refuses
         * unaccounted active formats; do not mask the actual SCTLR comparison. */
        context.shared_asid=1;
        smmuMapping={reinterpret_cast<volatile unsigned char *>(smmu.base),smmu.bytes};
        r=ArmSmmu4KBindMmio(&smmuMapping,&smmuRawIo);
        if(r){PongProbeRecord(4,"apps SMMU",Fail,"MMIO helper binding failed",r);return false;}
        smmuIo=smmuRawIo;smmuIo.cookie=this;smmuIo.polled_faults=1;
        smmuIo.read32=smmuRead32Forward;smmuIo.read64=smmuRead64Forward;
        smmuIo.write32=smmuWrite32Trace;smmuIo.write64=smmuWrite64Trace;smmuIo.barrier=smmuBarrierForward;
        ArmSmmu4KContextImage image={};
        r=ArmSmmu4KEncodeContext(&context,&image);
        if(r){PongProbeRecord(4,"apps SMMU",Fail,"context prerequisites unsupported; no register writes",r);return false;}
        if(!mappingMastersQuiescent())return false;
        const unsigned cb=(pages+bank)*4096;
        const uint32_t actlr=smmu.read(cb+4);
        if(smmu.read(0x20)!=id0 || smmu.read(0x24)!=id1 || smmu.read(0x48) ||
           (smmu.read(cb)&1) || smmu.read(cb+0x58)) {
            PongProbeRecord(4,"apps SMMU",Skip,"SMMU/bank state changed before first write; no programming");return false;
        }
        for(unsigned k=0;k<2;k++) {
            uint32_t smr=smmu.read(0x800+4*slots[k]),route=smmu.read(0xc00+4*slots[k]);
            if(ex?(route&(1u<<10)):(smr&(1u<<31))) {
                PongProbeRecord(4,"apps SMMU",Skip,"selected slot became valid; no programming",slots[k],route);return false;
            }
        }
        /* QUP/GPI has no ACTLR override in the SM8450 qcom client match list:
         * preserve the existing implementation-defined setting, not GPU32b. */
        IOLog("PONG INPUT NATIVE: SMMU LEASE selected ringSlot=%u dataSlot=%u bank=%u ASID=%u ACTLR=%08x; fresh locked checks complete\n",
            slots[0],slots[1],bank,asid,actlr);
        /* Hardware ownership is exclusive and all APPS GPI/other SE DMA was
         * checked quiescent. Fault ONLY these two exact stream slots first. */
        resourcesRetained=true;streamsChanged=true;
        for(unsigned k=0;k<2;k++) {
            uint32_t fault=(2u<<16)|(ex?(oldRoutes[k]&(1u<<10)):0u);
            ownedStreamWrite(0xc00+4*slots[k],fault);
            if(smmu.read(0xc00+4*slots[k])!=fault) {
                PongProbeRecord(4,"apps SMMU",Fail,"fault-route readback mismatch",slots[k],smmu.read(0xc00+4*slots[k]));return false;
            }
        }
        ArmSmmu4KProgramResult result={};
        PongProbeRecord(14,"PHOTO M02",Running,"before owned native4K context programming; no GPI/DMA start",bank,asid);
        r=ArmSmmu4KProgramDisabledContext(&smmuIo,bank,&context,10000,&result);
        if(r){PongProbeRecord(4,"apps SMMU",Fail,"owned context programming",r,result.fault_status);
            IOLog("PONG INPUT NATIVE: SMMU program FAIL phase=%u writes=%u polls=%u retain=%u\n",result.phase,result.writes,result.poll_reads,result.must_retain_tables);
            mappingReadbackDiagnostic(image,result);return false;}
        if(smmu.read(cb+4)!=actlr) {
            PongProbeRecord(4,"apps SMMU",Fail,"ACTLR changed during context programming; retain resources",actlr,smmu.read(cb+4));return false;
        }
        for(unsigned k=0;k<2;k++) {
            ownedStreamWrite(0x800+4*slots[k],(ex?0u:1u<<31)|sids[k]);
            ownedStreamWrite(0xc00+4*slots[k],bank|(ex?1u<<10:0));
            if(smmu.read(0x800+4*slots[k])!=((ex?0u:1u<<31)|sids[k]) ||
               smmu.read(0xc00+4*slots[k])!=(bank|(ex?1u<<10:0))) {
                PongProbeRecord(4,"apps SMMU",Fail,"translation-route readback mismatch",slots[k],smmu.read(0xc00+4*slots[k]));return false;
            }
        }
        if(!smmuHealthy()) {
            PongProbeRecord(4,"apps SMMU",Fail,"owned context unhealthy after routing");return false;
        }
        PongProbeRecord(4,"apps SMMU",Pass,"two exact streams on owned native 4K bank",bank,
                        uint64_t(slots[0])|(uint64_t(slots[1])<<32));return true;
    }
    bool smmuHealthy() {
        ArmSmmu4KProgramResult result={};
        uint32_t global=smmu.read(0x48);
        ArmSmmu4KStatus status=ArmSmmu4KObserveOwnedContext(&smmuIo,bank,&context,&result);
        if(!global && status==ArmSmmu4KSuccess)return true;
        PongProbeRecord(12,"DMA context",Fail,"global/owned context fault or changed registers",
                        uint64_t(global)|(uint64_t(result.fault_status)<<32),status);
        return false;
    }
    void reportIrqFailure() {
        /* First-error snapshot is immutable after the IRQ's release publish.
         * Never log, allocate or take the probe writer into interrupt context. */
        if(irqFailureReported || __atomic_load_n(&irqFailureState,__ATOMIC_ACQUIRE)!=2)return;
        irqFailureReported=true;const IrqFailure &f=irqFailure;
        IOLog("PONG INPUT NATIVE: GPI IRQ before-clear type=%08x global=%08x general=%08x ERROR_LOG=%08x command=%08x IRQs=%llu\n",
            f.type,f.global,f.general,f.log,f.command,f.count);
        IOLog("PONG INPUT NATIVE: GPI IRQ before-clear TX=%08x RX=%08x event=%08x TXscratch=%08x RXscratch=%08x\n",
            f.tx,f.rx,f.event,f.txScratch,f.rxScratch);
        PongProbeRecord(12,"GPI IRQ error",Fail,"captured before clear; a=type/global b=general/ERROR_LOG",
            uint64_t(f.type)|(uint64_t(f.global)<<32),uint64_t(f.general)|(uint64_t(f.log)<<32));
        PongProbeRecord(13,"GPI IRQ contexts",Fail,"before-clear context states; a=TX/RX b=event/command",
            uint64_t(f.tx)|(uint64_t(f.rx)<<32),uint64_t(f.event)|(uint64_t(f.command)<<32));
    }
    void quarantine() {
        releaseInput();
        if(reportReadyOwned && tlmm.read(ReportCfg)==reportWantedCfg)tlmm.write(ReportCfg,reportOldCfg);
        reportReadyOwned=false;
        reportIrqFailure();
        __atomic_store_n(&error,1,__ATOMIC_RELEASE);
        if(gpi.base && instance && gpiIrqAttached)gpi.write(c(0x23088),0);
        if(streamsChanged && pong_smmu_lease_lock()) {
            const uint16_t sids[]={RingSid,DataSid};
            for(unsigned k=0;k<2;k++) {
                // Keep an active EXIDS match valid while faulting it. Dropping
                // EXIDVALID could expose firmware's unmatched-stream policy.
                uint32_t route=smmu.read(0xc00+4*slots[k]);
                smmu.write(0xc00+4*slots[k],(2u<<16)|(exids?(route&(1u<<10)):0u));
                smmu.write(0x800+4*slots[k],(exids?0u:1u<<31)|sids[k]);
                smmu.write(0xc00+4*slots[k],(2u<<16)|(exids?1u<<10:0u));
            }
            pong_smmu_lease_unlock();
        }
        /* No memory, bank or stream slot is freed after a DMA attempt. */
    }
    static void interrupt(void *cookie) {
        Controller &d=*static_cast<Controller *>(cookie);
        __atomic_add_fetch(&d.irqCount,1,__ATOMIC_RELAXED);
        for(unsigned drain=0;drain<16;drain++) {
            uint32_t type=d.gpi.read(d.c(0x23080));if(!type)return;
            const bool fatal=(type&(4|64)) || (type&~0x4fu);
            uint32_t global=0,general=0;
            if(fatal) {
                /* Capture before ANY source acknowledgement, including a
                 * combined command/error IRQ. One bounded first-error record;
                 * hardware words are sequential reads, not a simultaneous latch. */
                global=d.gpi.read(d.c(0x23100));general=d.gpi.read(d.c(0x23118));
                unsigned empty=0;
                if(__atomic_compare_exchange_n(&d.irqFailureState,&empty,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
                    IrqFailure &f=d.irqFailure;
                    f.type=type;f.global=global;f.general=general;f.log=d.gpi.read(d.c(0x23200));
                    f.command=__atomic_load_n(&d.activeCommand,__ATOMIC_ACQUIRE);
                    f.tx=d.gpi.read(channel(d.instance,0));f.rx=d.gpi.read(channel(d.instance,1));
                    f.event=d.gpi.read(event(d.instance));
                    f.txScratch=d.gpi.read(channel(d.instance,0)+0x60);
                    f.rxScratch=d.gpi.read(channel(d.instance,1)+0x60);
                    f.count=__atomic_load_n(&d.irqCount,__ATOMIC_RELAXED);
                    __atomic_store_n(&d.irqFailureState,2,__ATOMIC_RELEASE);
                }
            }
            if(type&1) {
                uint32_t st=d.gpi.read(d.c(0x23090));d.gpi.write(d.c(0x230a0),st);
                for(unsigned k=0;k<2;k++)if(!fatal && (st&(1u<<k)))
                    __atomic_add_fetch(&d.channelAck[k],1,__ATOMIC_RELEASE);
            }
            if(type&2) {
                uint32_t st=d.gpi.read(d.c(0x23094));d.gpi.write(d.c(0x230a4),st);
                if(!fatal)__atomic_add_fetch(&d.eventAck,1,__ATOMIC_RELEASE);
            }
            if(type&8) {
                d.gpi.write(d.c(0x230c0),1);
                if(!fatal)__atomic_add_fetch(&d.eventNotifications,1,__ATOMIC_RELEASE);
            }
            if(fatal) {
                if(type&4) {
                    d.gpi.write(d.c(0x23110),global);
                    d.gpi.write(d.c(0x23200),0); // Linux acknowledgement, after capture
                }
                if(type&64)d.gpi.write(d.c(0x23128),general);
                d.gpi.write(d.c(0x23088),0);
                __atomic_store_n(&d.error,type,__ATOMIC_RELEASE);return;
            }
        }
        __atomic_store_n(&d.error,0xffffffffu,__ATOMIC_RELEASE);d.gpi.write(d.c(0x23088),0);
    }
    bool command(bool ev,unsigned ch,unsigned opcode,unsigned expectedState) {
        if(__atomic_load_n(&error,__ATOMIC_ACQUIRE)){reportIrqFailure();return false;}
        unsigned before=ev?__atomic_load_n(&eventAck,__ATOMIC_ACQUIRE):
            __atomic_load_n(&channelAck[ch],__ATOMIC_ACQUIRE);
        PongProbeRecord(9,"GPI command",Running,"before real command; a=PA b=opcode/channel/event",
            GpiPa+c(ev?0x23010:0x23008),uint64_t(opcode)|(uint64_t(ch)<<8)|(uint64_t(ev)<<16));
        __atomic_store_n(&activeCommand,opcode|(ch<<8)|(unsigned(ev)<<16),__ATOMIC_RELEASE);
        gpi.write(c(ev?0x23010:0x23008),(opcode<<24)|(ev?0:ch));
        uint64_t end=nowUs()+250000;
        do {
            if(__atomic_load_n(&error,__ATOMIC_ACQUIRE)){reportIrqFailure();return false;}
            unsigned ack=ev?__atomic_load_n(&eventAck,__ATOMIC_ACQUIRE):
                __atomic_load_n(&channelAck[ch],__ATOMIC_ACQUIRE);
            unsigned state=(gpi.read(ev?event(instance):channel(instance,ch))>>20)&15;
            if(ack!=before && state==expectedState) {
                if(__atomic_load_n(&error,__ATOMIC_ACQUIRE)){reportIrqFailure();return false;}
                PongProbeRecord(9,"GPI command",Pass,"actual IRQ acknowledgement and context state matched",state,ack);
                return true;
            }
            IOSleep(1);
        } while(nowUs()<end);
        unsigned ack=ev?__atomic_load_n(&eventAck,__ATOMIC_ACQUIRE):
            __atomic_load_n(&channelAck[ch],__ATOMIC_ACQUIRE);
        PongProbeRecord(9,"GPI command",Fail,"IRQ/state handshake timed out",
            uint64_t(opcode)|(uint64_t(ch)<<8)|(uint64_t(ev)<<16)|
            (uint64_t(gpi.read(ev?event(instance):channel(instance,ch)))<<32),
            uint64_t(before)|(uint64_t(ack)<<32));
        return false;
    }
    bool startGpi() {
        PongProbeRecord(5,"GPI IRQ",Running,"bind real level interrupt before allocation");
        uint32_t readback=0;uint64_t router=0;
        IOReturn r=armcpu_spi_attach(GpiIrqBase+instance,false,interrupt,this,&readback,&router);
        if(r){PongProbeRecord(5,"GPI IRQ",Fail,"IRQ ownership rejected",uint32_t(r));return false;}
        gpiIrqAttached=true;
        gpi.write(c(0x23088),0x4f);gpi.write(c(0x230b8),1);
        gpi.write(c(0x23098),3);gpi.write(c(0x2309c),1);
        gpi.write(c(0x23108),15);gpi.write(c(0x23120),15);
        gpi.write(c(0x23188),0);gpi.write(c(0x2318c),0);
        gpi.write(c(0x23400),0);gpi.write(c(0x23404),0);
        gpi.write(c(0x23180),1);gpi.write(c(0x23200),0);
        PongProbeRecord(5,"GPI IRQ",Pass,"real GIC route attached",GpiIrqBase+instance,readback);
        PongProbeRecord(6,"GPI rings",Running,"allocate/configure BOTH TX/RX before either START; actual IRQ acknowledgements");
        if(!command(true,0,0,1))return false;
        unsigned ev=event(instance);uint32_t evBase=DmaIova+EventRingOff;
        gpi.write(ev,16u<<24|1u<<16|2);gpi.write(ev+4,EventEntries*16);
        gpi.write(ev+8,evBase);gpi.write(ev+12,0);
        constexpr unsigned eventZeros[]={0x20,0x28,0x2c,0x30,0x34};
        for(unsigned off:eventZeros)gpi.write(ev+off,0);
        gpi.write(eventDoorbell(instance)+4,0);
        gpi.write(eventDoorbell(instance),evBase+eventCredit*16);
        for(unsigned k=0;k<2;k++) {
            if(!command(false,k,0,1))return false;
            unsigned ch=channel(instance,k),base=DmaIova+(k?RxRingOff:TxRingOff);
            gpi.write(ch,16u<<24|(k?0:1u<<3)|2);gpi.write(ch+4,TransferEntries*16);
            gpi.write(ch+8,base);gpi.write(ch+12,0);gpi.write(doorbell(instance,k)+4,0);
            gpi.write(ch+0x60,(uint32_t(k==0)<<16)|(GpiSpiProtocol<<4)|SeId);
            constexpr unsigned channelZeros[]={0x64,0x68,0x6c};
            for(unsigned off:channelZeros)gpi.write(ch+off,0);
            gpi.write(ch+0x5c,1);
            IOLog("PONG INPUT NATIVE: GPI paired configured channel=%u context=%08x ring=%08x length=%u scratch=%08x\n",
                k,gpi.read(ch),base,TransferEntries*16,gpi.read(ch+0x60));
        }
        /* SPI channels are a pair. Match gpi_ch_init(): allocate/configure
         * ALL channels first, then START each with real ACK/state validation. */
        PongProbeRecord(6,"GPI paired setup",Pass,"both channels configured; no START or SPI submission yet",
            gpi.read(channel(instance,0)),gpi.read(channel(instance,1)));
        for(unsigned k=0;k<2;k++) {
            if(!command(false,k,1,2))return false;started[k]=true;
        }
        PongProbeRecord(6,"GPI rings",Pass,"both channels started; memory retained",irqCount);return true;
    }
    uint32_t queue(unsigned ch,const Element &e) {
        unsigned &index=ch?rxWrite:txWrite,off=ch?RxRingOff:TxRingOff;
        auto ring=reinterpret_cast<Element *>(dma.cpu+off);
        ring[index]=e;uint32_t address=DmaIova+off+index*16;
        index=(index+1)%TransferEntries;return address;
    }
    bool events(uint32_t txEnd,uint32_t rxEnd,unsigned bytes,bool &txDone,bool &rxDone) {
        unsigned ev=event(instance);uint64_t head=uint64_t(gpi.read(ev+16))|(uint64_t(gpi.read(ev+20))<<32);
        uint32_t base=DmaIova+EventRingOff;
        if(head<base || head>=base+EventEntries*16 || ((head-base)&15)) {
            PongProbeRecord(11,"GPI event",Fail,"producer outside owned event ring",head,base);return false;
        }
        unsigned producer=unsigned(head-base)/16;
        cacheSync(dma.cpu+EventRingOff,EventEntries*16);
        for(unsigned budget=0;eventRead!=producer && budget<EventEntries;budget++) {
            Element e=reinterpret_cast<Element *>(dma.cpu+EventRingOff)[eventRead];
            if(eventLogCount<16) {
                ++eventLogCount;
                IOLog("PONG INPUT NATIVE: GPI actual event n=%u index=%u words=%08x %08x %08x %08x\n",
                    eventLogCount,eventRead,e.word[0],e.word[1],e.word[2],e.word[3]);
            }
            unsigned type=e.word[3]>>16&255,ch=e.word[3]>>24,code=e.word[2]>>24;
            uint64_t ptr=uint64_t(e.word[0])|(uint64_t(e.word[1])<<32);
            if(type==0x22) {
                if(ch>1 || (code!=1 && code!=2) || ptr!=(ch?rxEnd:txEnd) ||
                   (e.word[2]&0xffffff)!=bytes) {
                    PongProbeRecord(11,"GPI event",Fail,"completion pointer/code/length mismatch",
                        ptr,uint64_t(e.word[2])|(uint64_t(e.word[3])<<32));return false;
                }
                if(ch)rxDone=true;else txDone=true;
            } else if(type!=0x31 && type!=0xff) {
                PongProbeRecord(11,"GPI event",Fail,"unsupported event type",
                    ptr,uint64_t(e.word[2])|(uint64_t(e.word[3])<<32));return false;
            }
            eventRead=(eventRead+1)%EventEntries;eventCredit=(eventCredit+1)%EventEntries;
        }
        gpi.write(eventDoorbell(instance),base+eventCredit*16);
        return eventRead==producer;
    }
    bool transfer(const uint8_t *tx,uint8_t *rx,unsigned bytes) {
        if(!bytes || bytes>0x4000 || __atomic_load_n(&error,__ATOMIC_ACQUIRE) || !smmuHealthy())return false;
        memcpy(dma.cpu+TxDataOff,tx,bytes);bzero(dma.cpu+RxDataOff,bytes);
        uint32_t rxEnd=0;
        if(rx)rxEnd=queue(1,{{DmaIova+RxDataOff,0,bytes,0x10u<<16}});
        queue(0,{{4u|3u<<24,0,2,0x22u<<16|Chain}}); // 8-bit packed, TCXO/2
        queue(0,{{rx?3u:1u,0,0,0x20u<<16|Chain|(rx?Link:0u)}});
        uint32_t txEnd=queue(0,{{DmaIova+TxDataOff,0,bytes,0x10u<<16|Ieot}});
        cacheSync(dma.cpu,DmaBytes);
        unsigned notifications=__atomic_load_n(&eventNotifications,__ATOMIC_ACQUIRE);
        __atomic_store_n(&activeCommand,~0u,__ATOMIC_RELEASE); // transfer, not a context command
        ++transferSequence;
        if(!quietTransfers) {
            IOLog("PONG INPUT NATIVE: SPI actual submit sequence=%u bytes=%u duplex=%u TXend=%08x RXend=%08x\n",
                transferSequence,bytes,unsigned(rx!=nullptr),txEnd,rxEnd);
            PongProbeRecord(10,"PHOTO X01",Running,"before real paired-ring doorbells; a=TX last TRE b=RX last TRE",txEnd,rxEnd);
        }
        if(rx)gpi.write(doorbell(instance,1),DmaIova+RxRingOff+rxWrite*16);
        gpi.write(doorbell(instance,0),DmaIova+TxRingOff+txWrite*16);
        bool txDone=false,rxDone=!rx;uint64_t end=nowUs()+250000;
        do {
            if(__atomic_load_n(&error,__ATOMIC_ACQUIRE) || !events(txEnd,rxEnd,bytes,txDone,rxDone))return false;
            if(txDone && rxDone && __atomic_load_n(&eventNotifications,__ATOMIC_ACQUIRE)!=notifications) {
                if(!smmuHealthy())return false;
                if(rx){cacheSync(dma.cpu+RxDataOff,bytes);memcpy(rx,dma.cpu+RxDataOff,bytes);}
                if(!quietTransfers)PongProbeRecord(10,"SPI actual transfer",Pass,"real completion records, event IRQ and SMMU checks passed",bytes,irqCount);
                return true;
            }
            IOSleep(1);
        } while(nowUs()<end);
        PongProbeRecord(10,"SPI transfer",Fail,"completion/event IRQ timed out",
            uint64_t(txDone)|(uint64_t(rxDone)<<1)|(uint64_t(bytes)<<32),
            uint64_t(notifications)|(uint64_t(__atomic_load_n(&eventNotifications,__ATOMIC_ACQUIRE))<<32));
        return false;
    }
    bool readNovatek(uint32_t address,uint8_t *data,unsigned bytes) {
        reportPageSelected=false;
        if(!bytes || bytes>256 || address>=0x1000000 || bytes>0x1000000-address)return false;
        uint8_t page[]={0xff,uint8_t(address>>15),uint8_t(address>>7)};
        if(!transfer(page,nullptr,3))return false;
        uint8_t tx[258]={},rx[258]={};tx[0]=address&0x7f;
        if(!transfer(tx,rx,bytes+2))return false;
        memcpy(data,rx+2,bytes);return true;
    }
    bool writeNovatek(uint32_t address,const uint8_t *data,unsigned bytes) {
        reportPageSelected=false;
        if(!data || !bytes || bytes>4096 || address>=0x1000000 || bytes>0x1000000-address)return false;
        uint8_t page[]={0xff,uint8_t(address>>15),uint8_t(address>>7)};
        if(!transfer(page,nullptr,3))return false;
        protocolTx[0]=uint8_t(address&0x7f)|0x80;memcpy(protocolTx+1,data,bytes);
        return transfer(protocolTx,nullptr,bytes+1);
    }
    bool writeValue(uint32_t address,uint32_t value,unsigned bytes) {
        if(!bytes || bytes>4)return false;
        uint8_t data[]={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};
        return writeNovatek(address,data,bytes);
    }
    bool selectReportPage() {
        reportPageSelected=false;
        const uint8_t page[]={0xff,uint8_t(NvtEvent>>15),uint8_t(NvtEvent>>7)};
        if(!transfer(page,nullptr,sizeof(page)))return false;
        reportPageSelected=true;return true;
    }
    bool readReportBurst(uint8_t *data,unsigned bytes) {
        /* Vendor work_func calls CTP_SPI_READ directly on the event page;
         * no memory/page write is inserted before each ready frame. The
         * index+dummy layout is identical to the proven native memory read. */
        if(!reportPageSelected || !data || bytes!=NvtReportBytes)return false;
        uint8_t tx[NvtReportBytes+2]={},rx[NvtReportBytes+2]={};
        if(!transfer(tx,rx,sizeof(tx)))return false;
        memcpy(data,rx+2,bytes);return true;
    }
    void firmwareSnapshot(const char *reason) {
        if(__atomic_load_n(&error,__ATOMIC_ACQUIRE)){reportIrqFailure();return;}
        uint8_t flags[2]={},crc[8]={},state[6]={};
        if(!readNovatek(NvtCrcFlags,flags,2) || !readNovatek(0x1fb520,crc,8) || !readNovatek(NvtResetState,state,6))return;
        IOLog("PONG INPUT NATIVE: native firmware snapshot %s flags=%02x %02x ILM=%08x DLM=%08x reset=%02x %02x %02x %02x %02x %02x\n",
            reason,flags[0],flags[1],nvtLe32(crc),nvtLe32(crc+4),state[0],state[1],state[2],state[3],state[4],state[5]);
        PongProbeRecord(8,"native firmware snapshot",Fail,reason,
            uint64_t(nvtLe32(crc))|(uint64_t(nvtLe32(crc+4))<<32),
            uint64_t(flags[0])|(uint64_t(flags[1])<<8)|(uint64_t(state[0])<<16));
    }
    bool verifyFirmwareBytes() {
        /* BLD R_DLM is reused by the cascade downloader: device50 equals the
         * last Header section CRC, not primary DLM. Prove payload integrity
         * directly while the MCU is idle, BEFORE BOOT_RDY/running mutable RAM. */
        unsigned verified=0;uint8_t actual[256]={};
        PongProbeRecord(7,"native firmware bytes",Running,"bounded actual SRAM readback before boot; no retry/reload",firmwarePlan.total);
        for(unsigned k=0;k<firmwarePlan.count;k++) {
            const FirmwareSection &s=firmwarePlan.sections[k];if(!s.bytes)continue;
            for(unsigned at=0;at<s.bytes;) {
                unsigned bytes=s.bytes-at;if(bytes>sizeof(actual))bytes=sizeof(actual);
                if(!readNovatek(s.destination+at,actual,bytes))return false;
                const uint8_t *expected=nativeFirmware+s.source+at;
                for(unsigned i=0;i<bytes;i++)if(actual[i]!=expected[i]) {
                    PongProbeRecord(7,"native firmware bytes",Fail,"actual SRAM payload mismatch; no boot/input",
                        uint64_t(s.destination+at+i)|(uint64_t(k)<<32),uint64_t(expected[i])|(uint64_t(actual[i])<<8));return false;
                }
                at+=bytes;verified+=bytes;
            }
            IOLog("PONG INPUT NATIVE: actual SRAM bytes section=%u destination=%08x bytes=%u exact match before boot\n",k,s.destination,s.bytes);
        }
        if(verified!=firmwarePlan.total)return false;
        PongProbeRecord(7,"native firmware bytes",Pass,"all active SRAM payload bytes match input before boot",verified);return true;
    }
    bool loadFirmware(unsigned &maxX,unsigned &maxY) {
        if(!nvtFirmwarePlan(nativeFirmware,nativeFirmwareBytes,firmwarePlan)) {
            PongProbeRecord(7,"native firmware",Skip,"local firmware missing or unsafe cascade container; no chip reset/write");return false;
        }
        quietTransfers=true;
        PongProbeRecord(7,"native firmware",Running,"fresh bounded cascade loader; one attempt, no old loader/recovery",
            firmwarePlan.active,firmwarePlan.total);
        /* Board vendor nvt_update_firmware hw_crc2 branch: reset69, ILM/DLM
         * descriptors, autocopy56, all active sections, wait mode2, clear reset
         * and host AE00, BOOT_RDY1. No nonexistent BLD_CRC_EN/POR write. */
        if(!writeValue(NvtReset,0x69,1))return false;IOSleep(5);
        constexpr uint32_t destination[]={0x1fb528,0x1fb52c},length[]={0x1fb518,0x1fb530},golden[]={0x1fb500,0x1fb504};
        for(unsigned k=0;k<2;k++) {
            const FirmwareSection &s=firmwarePlan.sections[k];
            if(!writeValue(destination[k],s.destination,3) || !writeValue(length[k],s.bytes-1,3) || !writeValue(golden[k],s.crc,4))return false;
            uint8_t d[3]={},n[3]={},g[4]={};
            if(!readNovatek(destination[k],d,3) || !readNovatek(length[k],n,3) || !readNovatek(golden[k],g,4))return false;
            const unsigned actualDestination=unsigned(d[0])|(unsigned(d[1])<<8)|(unsigned(d[2])<<16);
            const unsigned actualLength=unsigned(n[0])|(unsigned(n[1])<<8)|(unsigned(n[2])<<16);
            if(actualDestination!=s.destination || actualLength!=s.bytes-1 || nvtLe32(g)!=s.crc) {
                PongProbeRecord(7,"native CRC bank",Fail,"descriptor/length/golden readback mismatch before download",k,nvtLe32(g));return false;
            }
            IOLog("PONG INPUT NATIVE: native CRC bank=%u destination=%08x length=%08x golden=%08x exact readback\n",
                k,actualDestination,actualLength,nvtLe32(g));
        }
        if(!writeValue(NvtAutoCopy,0x56,1))return false;
        for(unsigned k=0;k<firmwarePlan.count;k++) {
            const FirmwareSection &s=firmwarePlan.sections[k];if(!s.bytes)continue;
            PongProbeRecord(7,"native firmware section",Running,"real native GPI SRAM download; a=section/destination b=bytes",
                uint64_t(k)|(uint64_t(s.destination)<<32),s.bytes);
            for(unsigned at=0;at<s.bytes;) {
                unsigned chunk=s.bytes-at;if(chunk>4096)chunk=4096;
                if(!writeNovatek(s.destination+at,nativeFirmware+s.source+at,chunk))return false;
                at+=chunk;
            }
            IOLog("PONG INPUT NATIVE: native firmware section=%u source=%08x destination=%08x bytes=%u actual transfers=%u\n",
                k,s.source,s.destination,s.bytes,transferSequence);
        }
        uint8_t copy=255;unsigned polls=0;
        for(;polls<200;polls++) {
            if(!readNovatek(NvtAutoCopy,&copy,1))return false;
            if(!copy)break;IOSleep(1);
        }
        PongProbeRecord(7,"native auto-copy",copy?Fail:Pass,"hw_crc2 completion at TX_AUTO_COPY_EN, not SPI_DMA_TX_INFO",copy,polls);
        if(copy || !verifyFirmwareBytes())return false;
        const uint8_t clear[6]={},host[]={0xae,0};
        if(!writeNovatek(NvtResetState,clear,6) || !writeNovatek(NvtHost,host,2) || !writeValue(NvtBootReady,1,1))return false;
        IOSleep(5);
        uint8_t state[6]={};bool ready=false;
        for(unsigned i=0;i<150;i++) {
            if(!readNovatek(NvtResetState,state,6))return false;
            if(state[0]>=0xa0 && state[0]<=0xaf){ready=true;break;}IOSleep(10);
        }
        IOLog("PONG INPUT NATIVE: native firmware reset-state=%02x %02x %02x %02x %02x %02x ready=%u\n",
            state[0],state[1],state[2],state[3],state[4],state[5],unsigned(ready));
        if(!ready)return false;
        uint8_t flags[2]={},crc[8]={},info[38]={};
        if(!readNovatek(NvtCrcFlags,flags,2) || !readNovatek(0x1fb520,crc,8) || !readNovatek(NvtFwInfo,info,38))return false;
        const bool crcOk=(flags[0]&7)==4;
        const bool geometryOk=nvtGeometry(info,sizeof(info),firmwarePlan.version,maxX,maxY);
        IOLog("PONG INPUT NATIVE: native firmware boot CRC flags=%02x %02x done/no-error=%u R_ILM=%08x R_DLM=%08x (cascade scratch, not primary DLM verdict)\n",
            flags[0],flags[1],unsigned(crcOk),nvtLe32(crc),nvtLe32(crc+4));
        IOLog("PONG INPUT NATIVE: native firmware FWINFO version=%02x inverse=%02x channels=%u/%u sensor=%u/%u expectedVersion=%02x geometryMatch=%u\n",
            info[0],info[1],info[2],info[3],nvtBe16(info+4),nvtBe16(info+6),firmwarePlan.version,unsigned(geometryOk));
        if(!crcOk || !geometryOk)return false;
        PongProbeRecord(7,"native firmware",Pass,"exact preboot SRAM/golden banks, autocopy, reset, boot CRC flags and FW sensor geometry validated",
            uint64_t(maxX)|(uint64_t(maxY)<<32),info[0]);return true;
    }
    bool observeReports(unsigned maxX,unsigned maxY) {
        if(!selectReportPage())return false;
        PongProbeRecord(15,"native reports",Running,"90-second bounded SPI report observation; no report IRQ/HID/pen advertised",maxX,maxY);
        uint8_t data[NvtReportBytes]={},previous[NvtReportBytes]={};bool havePrevious=false;
        unsigned valid=0,bad=0,changed=0,contactChanges=0,logs=0,streak=0;
        uint64_t end=nowUs()+90000000,nextSummary=nowUs()+10000000;
        for(unsigned budget=0;budget<9000 && nowUs()<end;budget++) {
            if(!readReportBurst(data,sizeof(data)))return false;
            NativeReport report;
            if(!nvtReport(data,sizeof(data),maxX,maxY,report)) {
                ++bad;if(++streak>=32) {
                    PongProbeRecord(15,"native reports",Fail,"32 invalid report samples; stop, no reload/recovery",valid,bad);return false;
                }
            } else {
                ++valid;streak=0;
                if(!havePrevious || memcmp(data,previous,sizeof(data))) {
                    ++changed;memcpy(previous,data,sizeof(data));havePrevious=true;
                    if(report.count) {
                        ++contactChanges;
                        if(logs<12) {
                            ++logs;const NativeContact &p=report.contacts[0];
                            IOLog("PONG INPUT NATIVE: native report changed sample=%u contacts=%u first=id%u x%u y%u pressure%u checksum=%02x\n",
                                changed,report.count,p.id,p.x,p.y,p.pressure,data[64]);
                        }
                    }
                }
            }
            if(nowUs()>=nextSummary) {
                IOLog("PONG INPUT NATIVE: native report samples valid=%u bad=%u changed=%u contactChanges=%u (poll samples, NOT report IRQ count)\n",
                    valid,bad,changed,contactChanges);nextSummary=nowUs()+10000000;
            }
            IOSleep(10);
        }
        PongProbeRecord(15,"native reports",contactChanges?Pass:Skip,
            contactChanges?"validated changed contact samples over real SPI; no HID/pen or sustained-driver verdict":
                "observation ended without changed contact sample; no fabricated input",
            uint64_t(valid)|(uint64_t(bad)<<32),uint64_t(changed)|(uint64_t(contactChanges)<<32));return true;
    }
    void releaseInput() {
        if(touchPublished)for(unsigned k=0;k<10;k++)if(contactMask&(1u<<k)) {
            const NativeContact &p=lastContacts[k];pong_touch_post(3,k,p.x,p.y);
        }
        contactMask=0;
        if(penPublished && (lastPen.range || lastPen.touch)) {
            pong_native_pen sample={};sample.time=mach_absolute_time();sample.x=lastPen.x;sample.y=lastPen.y;
            pong_native_pen_post(&sample);
        }
        lastPen=NativePen{};
    }
    bool enablePen() {
        const uint8_t command[]={0x7b,1};
        PongProbeRecord(8,"native pen configuration",Running,"vendor event-page7b enable, no Bluetooth/helper/battery writes",NvtHost);
        if(!writeNovatek(NvtHost,command,2))return false;
        uint8_t ack=255;
        for(unsigned k=0;k<50;k++) {
            IOSleep(10);if(!readNovatek(NvtHost,&ack,1))return false;
            if(!ack) {
                PongProbeRecord(8,"native pen configuration",Pass,"actual host-command acknowledgement; pen samples still unverified");return true;
            }
        }
        PongProbeRecord(8,"native pen configuration",Fail,"host-command acknowledgement timed out; touch retained, no pen published",ack);return false;
    }
    void tracePenPacket(const uint8_t *data,PenResult result,const NativePen &pen) {
        /* Per-class budgets keep initial hover from consuming all tip/reject
         * evidence. This only observes the unchanged decoder decision. */
        const uint8_t *p=data+65;const unsigned rawPressure=nvtBe16(p+5);
        if(p[0]==1 && rawPressure>penRawPeak)penRawPeak=rawPressure;
        if(result<0)++penRejected[unsigned(-int(result)-1)];
        else if(result==PenOut)++penOutSamples;
        else if(result==PenId)++penIdSamples;
        else {
            if(pen.touch)++penTipSamples;else ++penHoverSamples;
            if(pen.pressure>penAcceptedPeak)penAcceptedPeak=pen.pressure;
        }
        bool log=false;
        if(result==PenOut && penOutSamples<=2)log=true;
        if(result<0 && penRawRejectLogs<8){++penRawRejectLogs;log=true;}
        if(result==PenData && pen.touch && penRawTipLogs<8){++penRawTipLogs;log=true;}
        if(result==PenData && !pen.touch && penRawHoverLogs<2){++penRawHoverLogs;log=true;}
        if(result==PenData || result==PenOut) {
            if((pen.range!=penTraceRange || pen.touch!=penTraceTouch) && penRawTransitionLogs<20) {
                ++penRawTransitionLogs;log=true;
            }
            penTraceRange=pen.range;penTraceTouch=pen.touch;
        }
        if(log)IOLog("PONG INPUT NATIVE: pen packet result=%d fmt=%02x rawXY=%u/%u pressure=%u distance=%u tiltRaw=%d/%d tiltValid=%u accepted=%u/%u raw65=%08x %08x %08x %02x%02x peak=%u/%u\n",
            int(result),p[0],nvtBe16(p+1),nvtBe16(p+3),rawPressure,nvtBe16(p+9),int(int8_t(p[7])),int(int8_t(p[8])),unsigned(pen.tiltValid),
            pen.range,pen.touch,nvtLe32(p),nvtLe32(p+4),nvtLe32(p+8),p[12],p[13],penRawPeak,penAcceptedPeak);
    }
    void penSummary() {
        IOLog("PONG INPUT NATIVE: pen totals hover=%u tip=%u out=%u id=%u rawPeak=%u acceptedPeak=%u rejects(length/geometry/format/xy/pressure/reserved)=%u/%u/%u/%u/%u/%u (orientation never drops XY/pressure)\n",
            penHoverSamples,penTipSamples,penOutSamples,penIdSamples,penRawPeak,penAcceptedPeak,
            penRejected[0],penRejected[1],penRejected[2],penRejected[3],penRejected[4],penRejected[5]);
    }
    bool penStatusSnapshot() {
        /* Read-only pen-source diagnosis. Host bytes are raw last-command
         * data, not proof that the sensor has detected/enabled a pen. Always
         * restore the report page after these addressed reads. No re-enable,
         * firmware reload, battery operation or changed hand parsing. */
        uint8_t host[3]={},state[6]={},info[8]={};
        const bool readOkay=readNovatek(NvtHost,host,sizeof(host)) &&
            readNovatek(NvtResetState,state,sizeof(state)) && readNovatek(NvtFwInfo,info,sizeof(info));
        const bool pageOkay=selectReportPage();
        if(!readOkay || !pageOkay) {
            PongProbeRecord(8,"pen source snapshot",Fail,"read-only status or report-page restore failed",readOkay,pageOkay);return false;
        }
        IOLog("PONG INPUT NATIVE: pen source snapshot hostRaw=%02x %02x %02x reset=%02x %02x fw=%02x/%02x sensors=%u/%u GPIO=%08x cfg=%08x pending=%08x lastAckRead=%08x (raw state, not pen-detection proof)\n",
            host[0],host[1],host[2],state[0],state[1],info[0],info[1],nvtBe16(info+4),nvtBe16(info+6),
            tlmm.read(ReportIo),tlmm.read(ReportCfg),tlmm.read(ReportStatus),lastReportAck);
        return true;
    }
    bool prepareReportReady() {
        const uint32_t ctl=tlmm.read(ReportCtl),cfg=tlmm.read(ReportCfg),status=tlmm.read(ReportStatus),io=tlmm.read(ReportIo);
        IOLog("PONG INPUT NATIVE: report GPIO21 before ctl=%08x cfg=%08x status=%08x io=%08x PA=%llx\n",
            ctl,cfg,status,io,TlmmPa+ReportCfg);
        if(cfg&1) {
            PongProbeRecord(14,"report readiness",Skip,"GPIO21 interrupt already enabled; preserve unknown owner, no takeover",cfg,status);return false;
        }
        /* GPIO21 input/mux only. Raw rising-edge status latch is enabled, but
         * interrupt-enable bit0 remains ZERO: no unowned TLMM/PDC GIC interrupt.
         * Keep target/wakeup/global controller policy and every other pin. */
        reportOldCfg=cfg;reportWantedCfg=(cfg&~0x1fu)|0x16u;
        const uint32_t wantedCtl=ctl&~(0x3cu|(1u<<9));
        PongProbeRecord(14,"report readiness",Running,"owned GPIO21 raw rising-edge latch only; no GIC/PDC IRQ enable",TlmmPa+ReportCfg,reportWantedCfg);
        reportReadyOwned=true;
        tlmm.write(ReportCtl,wantedCtl);tlmm.write(ReportCfg,reportWantedCfg);
        if(tlmm.read(ReportCtl)!=wantedCtl || tlmm.read(ReportCfg)!=reportWantedCfg) {
            PongProbeRecord(14,"report readiness",Fail,"GPIO21 input/raw latch readback mismatch; no report read");return false;
        }
        const uint32_t beforeAck=tlmm.read(ReportStatus);
        tlmm.write(ReportStatus,ReportStatusAck); // SM8475 ack-low, NOT write-one-to-clear
        const uint32_t afterAck=tlmm.read(ReportStatus);
        IOLog("PONG INPUT NATIVE: GPIO21 initial ack-low value=%08x status=%08x/%08x io=%08x\n",
            ReportStatusAck,beforeAck,afterAck,tlmm.read(ReportIo));
        PongProbeRecord(14,"report readiness",Pass,"GPIO21 ack-low setup; raw polling, actual new edge still required",beforeAck,afterAck);return true;
    }
    bool waitReportNormal() {
        uint8_t state[6]={};unsigned last=~0u;
        for(unsigned k=0;k<500;k++) {
            if(!readNovatek(NvtResetState,state,6))return false;
            if(state[0]!=last) {
                last=state[0];IOLog("PONG INPUT NATIVE: report startup state=%02x GPIO21=%08x rawStatus=%08x\n",
                    state[0],tlmm.read(ReportIo),tlmm.read(ReportStatus));
            }
            if(state[0]>=0xa3 && state[0]<=0xaf) {
                PongProbeRecord(15,"report normal state",Pass,"actual RESET_STATE_NORMAL_RUN reached before sampling",state[0],k);return true;
            }
            IOSleep(10);
        }
        PongProbeRecord(15,"report normal state",Fail,"normal-run state timeout; do not sample uninitialized reports",state[0],tlmm.read(ReportStatus));return false;
    }
    void reportSnapshot(const uint8_t *data,ReportResult reason,bool ready,unsigned level,unsigned status) {
        unsigned sum=0;for(unsigned k=0;k<64;k++)sum+=data[k];
        IOLog("PONG INPUT NATIVE: report raw ready=%u reason=%u GPIO21=%u status=%08x checksum=%02x/%02x first=%08x %08x %08x %08x tail=%08x pen=%02x force=%02x %02x\n",
            unsigned(ready),unsigned(reason),level,status,unsigned(uint8_t(-sum)),data[64],nvtLe32(data),nvtLe32(data+4),
            nvtLe32(data+8),nvtLe32(data+12),nvtLe32(data+60),data[65],data[98],data[99]);
        IOLog("PONG INPUT NATIVE: report raw pen words=%08x %08x %08x; reason0=valid 1=len 2=geometry 3=checksum 4=id 5=duplicate 6=coordinate 7=startup\n",
            nvtLe32(data+65),nvtLe32(data+69),nvtLe32(data+73));
        for(unsigned at=0;at<96;at+=16)IOLog("PONG INPUT NATIVE: report raw offset=%u words=%08x %08x %08x %08x\n",
            at,nvtLe32(data+at),nvtLe32(data+at+4),nvtLe32(data+at+8),nvtLe32(data+at+12));
        IOLog("PONG INPUT NATIVE: report raw offset=96 words=%08x %08x %08x\n",nvtLe32(data+96),nvtLe32(data+100),nvtLe32(data+104));
        PongProbeRecord(13,"report raw snapshot",Running,"a=first8 raw bytes b=reason/checksum/level/status",
            uint64_t(nvtLe32(data))|(uint64_t(nvtLe32(data+4))<<32),uint64_t(reason)|(uint64_t(data[64])<<8)|
            (uint64_t(uint8_t(-sum))<<16)|(uint64_t(level)<<24)|(uint64_t(status)<<32));
    }
    bool runInput(unsigned maxX,unsigned maxY) {
        if(!prepareReportReady() || !waitReportNormal())return false;
        bool penEnabled=enablePen();
        if(__atomic_load_n(&error,__ATOMIC_ACQUIRE))return false;
        if(!selectReportPage())return false;
        if(!pong_touch_device_ready()) {
            PongProbeRecord(15,"native input",Fail,"generic touch device publication failed; no input fabricated");return false;
        }
        touchPublished=true;
        if(penEnabled)penPublished=pong_native_pen_device_ready();
        PongProbeRecord(15,"native input",Running,"GPIO21 ack-low ready acquisition; direct event-page SPI bursts, report IRQ not claimed",
            touchPublished,penPublished);
        uint8_t data[108]={};unsigned valid=0,bad=0,streak=0,penStreak=0;
        unsigned diagnosticReads=0,lastLevel=tlmm.read(ReportIo)&1;
        uint64_t nextSummary=nowUs()+10000000,nextDiagnostic=nowUs()+2000000,nextPenStatus=nowUs()+15000000;
        unsigned penStatusCount=0;
        /* Functional driver worker, not an unbounded optional photo probe.
         * Polling is explicit; GPI completion IRQs remain real. No GPIO SPI,
         * synthetic report IRQ, firmware reload or transport retry fallback. */
        while(!__atomic_load_n(&error,__ATOMIC_ACQUIRE)) {
            const unsigned status=tlmm.read(ReportStatus),level=tlmm.read(ReportIo)&1;
            const bool ready=(status&1) || (level && !lastLevel);lastLevel=level;
            const bool diagnostic=!ready && diagnosticReads<8 && nowUs()>=nextDiagnostic;
            if(!ready && !diagnostic) {
                if(penStatusCount<4 && !penHoverSamples && !penTipSamples && nowUs()>=nextPenStatus) {
                    ++penStatusCount;nextPenStatus=nowUs()+45000000;
                    if(!penStatusSnapshot())return false;
                }
                if(nowUs()>=nextSummary) {
                    penSummary();nextSummary=nowUs()+10000000;
                }
                IOSleep(1);continue;
            }
            if(status&1) {
                tlmm.write(ReportStatus,ReportStatusAck); // owned leaf ACK0; never self-assert READY with1
                lastReportAck=tlmm.read(ReportStatus); // preserve native53 MMIO ordering even with touch logs off
            }
            if(diagnostic){++diagnosticReads;nextDiagnostic=nowUs()+2000000;}
            if(!readReportBurst(data,sizeof(data)))return false;
            NativeReport report;
            const ReportResult result=nvtReportResult(data,sizeof(data),maxX,maxY,report);
            if(diagnostic)continue; // diagnostics are NEVER published as fresh hardware samples
            if(result==ReportStartup){streak=0;IOSleep(1);continue;}
            if(result!=ReportOkay) {
                ++bad;++streak;if(streak==8)releaseInput();
                if(streak>=32) {
                    PongProbeRecord(15,"native input",Fail,"32 invalid READY samples, source stopped; touch trace disabled, no reload",valid,bad);return false;
                }
                IOSleep(8);continue;
            }
            ++valid;streak=0;
            NativePen pen;const PenResult format=penPublished?nvtPenResult(data,sizeof(data),maxX,maxY,pen):PenId;
            if(penPublished)tracePenPacket(data,format,pen);
            if(format<0){if(++penStreak==8)releaseInput();}
            else if(format!=2)penStreak=0;
            if(format==0 || format==1) {
                pen.x=unsigned(uint64_t(pen.x)*65535/maxX);pen.y=unsigned(uint64_t(pen.y)*65535/maxY);
                if(format==0){pen.x=lastPen.x;pen.y=lastPen.y;}
                if(pen.range!=lastPen.range || pen.touch!=lastPen.touch || pen.x!=lastPen.x || pen.y!=lastPen.y ||
                   pen.pressure!=lastPen.pressure || pen.tiltX!=lastPen.tiltX || pen.tiltY!=lastPen.tiltY || pen.tiltValid!=lastPen.tiltValid) {
                    pong_native_pen sample={};sample.time=mach_absolute_time();sample.range=pen.range;sample.touch=pen.touch;
                    sample.x=pen.x;sample.y=pen.y;sample.pressure=pen.pressure;sample.tiltX=pen.tiltX;sample.tiltY=pen.tiltY;sample.distance=pen.distance;
                    sample.reserved=pen.tiltValid?PONG_PEN_TILT_VALID:0;
                    pong_native_pen_post(&sample);lastPen=pen;
                }
            }
            unsigned nextMask=0;
            /* When a verified pen is in range, release finger/palm contacts. */
            if(!lastPen.range)for(unsigned i=0;i<report.count;i++) {
                NativeContact p=report.contacts[i];unsigned k=p.id-1;
                p.x=unsigned(uint64_t(p.x)*65535/maxX);p.y=unsigned(uint64_t(p.y)*65535/maxY);nextMask|=1u<<k;
                unsigned type=(contactMask&(1u<<k))?2:1;
                if(type==1 || p.x!=lastContacts[k].x || p.y!=lastContacts[k].y) {
                    pong_touch_post(type,k,p.x,p.y);
                }
                lastContacts[k]=p;
            }
            for(unsigned k=0;k<10;k++)if((contactMask&~nextMask)&(1u<<k)) {
                const NativeContact &p=lastContacts[k];pong_touch_post(3,k,p.x,p.y);
            }
            contactMask=nextMask;
            if(nowUs()>=nextSummary) {
                penSummary();nextSummary=nowUs()+10000000;
            }
            IOSleep(8);
        }
        return false;
    }
    void run() {
        IOLog("PONG INPUT NATIVE: independent GENI/GPI implementation mode=%u; GPIO SPI disabled\n",mode);
        if(mode==3) {
            PongProbeRecord(15,"native input",Running,
                "PHOTO mapping-only: no wrapper votes, GPI MMIO, SE4, SMMU or DMA",GpiPa,GpiBytes);
            if(!gpi.map(GpiPa,GpiBytes,true)) {
                PongProbeRecord(15,"native input",Fail,"PHOTO mapping failed; transport not attempted");return;
            }
            PongProbeRecord(15,"native input",Skip,
                "PHOTO DONE mapping-only; no registers accessed, no touch/pen published");
            return;
        }
        if(mode==4) {
            PongProbeRecord(15,"native input",Running,
                "PHOTO one-read: wrapper prerequisite, CPU map, one TX context load only",GpiPa+PhotoGpiReadOffset,4);
            if(!clocks()) {
                PongProbeRecord(15,"native input",Skip,"PHOTO clocks failed; no GPI mapping or read");return;
            }
            if(!gpi.map(GpiPa,GpiBytes,true)) {
                PongProbeRecord(15,"native input",Fail,"PHOTO mapping failed; no GPI read");return;
            }
            if(gpi.bytes<4 || PhotoGpiReadOffset>gpi.bytes-4) {
                PongProbeRecord(15,"native input",Skip,"PHOTO read outside mapped range; no MMIO");return;
            }
            PongProbeRecord(14,"PHOTO R01",Running,
                "before ONE GPI instance1 TX context read; no GPI writes or DMA",GpiPa+PhotoGpiReadOffset,PhotoGpiReadOffset);
            __asm__ volatile("dsb sy":::"memory");
            const uint32_t value=gpi.read(PhotoGpiReadOffset);
            __asm__ volatile("dsb sy":::"memory");
            PongProbeRecord(14,"PHOTO R02",Pass,
                "one context load returned; ownership and DMA NOT validated",value,GpiPa+PhotoGpiReadOffset);
            PongProbeRecord(15,"native input",Skip,
                "PHOTO DONE one-read only; no channel/IRQ/SMMU/DMA/input startup");
            return;
        }
        if(mode==5) {
            PongProbeRecord(15,"native input",Running,
                "PHOTO APPS scan: read-only GPI contexts; no other SE or DMA",AppsInstances,24);
            if(!clocks()) {
                PongProbeRecord(15,"native input",Skip,"PHOTO clocks failed; no GPI scan");return;
            }
            if(!gpi.map(GpiPa,GpiBytes,true)) {
                PongProbeRecord(15,"native input",Fail,"PHOTO mapping failed; no GPI scan");return;
            }
            PongProbeRecord(1,"GPI context scan",Running,
                "inspect allowed APPS only; no ownership lease or register writes");
            unsigned reads=0;
            for(unsigned n=1;n<=6;n++) {
                if(!(AppsInstances&(1u<<n)))continue;
                const unsigned offsets[]={channel(n,0),channel(n,1),event(n),control(n,0x23080)};
                uint32_t values[4]={};
                for(unsigned k=0;k<4;k++) {
                    unsigned off=offsets[k];
                    if((off&3) || gpi.bytes<4 || off>gpi.bytes-4) {
                        PongProbeRecord(1,"GPI context scan",Skip,"read outside mapped range; no MMIO",n,off);
                        PongProbeRecord(15,"native input",Skip,"PHOTO scan prerequisite failed");return;
                    }
                    PongProbeRecord(14,"PHOTO S01",Running,
                        "before scan read: a=PA b=instance:kind (TX0 RX1 EV2 IRQ3)",GpiPa+off,(uint64_t(n)<<32)|k);
                    __asm__ volatile("dsb sy":::"memory");
                    values[k]=gpi.read(off);
                    __asm__ volatile("dsb sy":::"memory");
                    ++reads;
                    PongProbeRecord(14,"PHOTO S02",Pass,
                        "scan load returned: a=PA b=value; no ownership claim",GpiPa+off,values[k]);
                    if((k<3 && ((values[k]>>20)&15)) || (k==3 && values[k])) {
                        PongProbeRecord(1,"GPI context scan",Skip,
                            k<3?"allocated context; no takeover":"pending firmware IRQ; no clearing",GpiPa+off,values[k]);
                        PongProbeRecord(15,"native input",Skip,"PHOTO scan found occupied/pending state; no further work");return;
                    }
                }
                IOLog("PONG INPUT NATIVE: APPS scan instance=%u TX=%08x RX=%08x EV=%08x IRQ=%08x reads=%u\n",
                    n,values[0],values[1],values[2],values[3],reads);
            }
            PongProbeRecord(1,"GPI context scan",Pass,
                "read-only APPS contexts observed unallocated; no lease claimed",AppsInstances,reads);
            PongProbeRecord(15,"native input",Skip,
                "PHOTO DONE GPI scan only; no other SE/IRQ/SMMU/DMA/input startup");return;
        }
        if(mode==6) {
            PongProbeRecord(15,"native input",Running,
                "PHOTO shared-SE check only; no GPI scan/writes, SE4 or DMA startup");
            if(!clocks()) {
                PongProbeRecord(15,"native input",Skip,"PHOTO wrapper clocks failed; no shared SE inspection");return;
            }
            if(!inspectSharedSe()) {
                PongProbeRecord(15,"native input",Skip,"PHOTO shared SE prerequisite failed; no further work");return;
            }
            PongProbeRecord(15,"native input",Skip,
                "PHOTO DONE shared-SE reads only; no GPI/SE4/SMMU/DMA/input startup");return;
        }
        if(mode==7) {
            PongProbeRecord(15,"native input",Running,
                "PHOTO SE4 clock and capabilities only; no pins/SE mode/IRQ/SMMU/DMA");
            if(!clocks() || !prepareSe4Clock()) {
                PongProbeRecord(15,"native input",Skip,"PHOTO target-clock prerequisites failed; no capability reads");return;
            }
            PongProbeRecord(14,"PHOTO T01",Running,"before SE4 CPU map; no SE writes",SePa,SeRegisterBytes);
            if(!se.map(SePa,SeRegisterBytes,true)) {
                PongProbeRecord(15,"native input",Fail,"PHOTO SE4 mapping failed; no capability reads");return;
            }
            PongProbeRecord(14,"PHOTO T02",Running,"before wrapper CPU map; no wrapper register writes",WrapperPa,0x2000);
            if(!wrapper.map(WrapperPa,0x2000,true)) {
                PongProbeRecord(15,"native input",Fail,"PHOTO wrapper mapping failed; no capability reads");return;
            }
            const unsigned offsets[]={0x68,0x40,0x64,SeDmaModeOffset,4};
            uint32_t values[5]={};
            for(unsigned k=0;k<5;k++) {
                const Window &w=k==4?wrapper:se;
                const unsigned off=offsets[k];
                const uint64_t pa=(k==4?WrapperPa:SePa)+off;
                if(w.bytes<4 || off>w.bytes-4) {
                    PongProbeRecord(15,"native input",Skip,"PHOTO capability read outside map; no MMIO",pa,k);return;
                }
                PongProbeRecord(14,"PHOTO T03",Running,
                    "before capability read: a=PA b=kind (FW0 STATUS1 IF2 DMA3 WRAP4)",pa,k);
                __asm__ volatile("dsb sy":::"memory");
                values[k]=w.read(off);
                __asm__ volatile("dsb sy":::"memory");
                PongProbeRecord(14,"PHOTO T04",Pass,"capability load returned: a=PA b=value; no SPI claim",pa,values[k]);
            }
            IOLog("PONG INPUT NATIVE: SE4 CAPS FW=%08x STATUS=%08x IF=%08x DMA=%08x WRAP=%08x CFG=%08x clockConfigured=%u\n",
                values[0],values[1],values[2],values[3],values[4],seClockConfig,unsigned(seClockConfigured));
            PongProbeRecord(2,"SE4 capabilities",Pass,
                "five read-only loads completed; firmware/protocol not endorsed",values[0],values[4]);
            PongProbeRecord(15,"native input",Skip,
                "PHOTO DONE SE4 capabilities only; no pin/SE mode/IRQ/SMMU/DMA/input startup");return;
        }
        if(mode==8) {
            PongProbeRecord(15,"native input",Running,
                "PHOTO SMMU read-only only; no clocks/SE/GPI/IRQ/DMA or input startup");
            if(!inspectSmmuRoutes()) {
                PongProbeRecord(15,"native input",Skip,"PHOTO SMMU prerequisite failed; no further work");return;
            }
            PongProbeRecord(15,"native input",Skip,
                "PHOTO DONE SMMU observations only; no leases/table/routes/DMA/input startup");return;
        }
        if(mode==9) {
            PongProbeRecord(15,"native input",Running,"PHOTO SMMU plan-only; no allocation/table/routes/clock/SE/GPI/IRQ/DMA");
            if(!inspectSmmuPlan()) {
                PongProbeRecord(15,"native input",Skip,"PHOTO SMMU plan unavailable; no takeover or further work");return;
            }
            PongProbeRecord(15,"native input",Skip,"PHOTO DONE SMMU plan observations; no allocation/reservation/DMA/input");return;
        }
        if(mode==10) {
            PongProbeRecord(15,"native input",Running,"real native4K mapping preparation only; no pins/SE mode/GPI start/DMA/input");
            if(!clocks() || !idleMasters() || !prepareSe4Clock() || !se.map(SePa,SeRegisterBytes,true)) {
                PongProbeRecord(15,"native input",Skip,"mapping prerequisites failed; no SMMU programming");return;
            }
            const uint32_t fw=se.read(0x68),status=se.read(0x40),interface=se.read(0x64),dmaMode=se.read(SeDmaModeOffset);
            IOLog("PONG INPUT NATIVE: mapping target FW=%08x STATUS=%08x IF=%08x DMA=%08x\n",fw,status,interface,dmaMode);
            if(((fw>>8)&255)!=1 || (status&0x1001) || !(interface&1) || !(dmaMode&1)) {
                PongProbeRecord(15,"native input",Skip,"target SPI/GPI prerequisites changed; no SMMU programming");return;
            }
            if(!translation()) {
                if(streamsChanged)quarantine();
                PongProbeRecord(15,"native input",Fail,"mapping preparation failed; no GPI/DMA start, resources retained");return;
            }
            PongProbeRecord(15,"native input",Pass,"native4K mapping readback ready; NO GPI/IRQ/DMA/Novatek/input attempt yet",bank,
                uint64_t(slots[0])|(uint64_t(slots[1])<<32));return;
        }
        if(!clocks() || !idleMasters() || !serialEngine() || !translation()) {
            PongProbeRecord(15,"native input",Skip,"prerequisite failed; no legacy fallback");
            if(streamsChanged)quarantine();return;
        }
        if(mode==1) {
            PongProbeRecord(15,"native input",Skip,"preflight only; no DMA started; ownership retained");return;
        }
        if(!startGpi()) {
            PongProbeRecord(6,"GPI rings",Fail,"command IRQ/state handshake failed",error,irqCount);quarantine();return;
        }
        PongProbeRecord(7,"Novatek ROM",Running,"real DMA page write and trim read; no GPIO transfer");
        uint8_t id[6]={};
        if(!readNovatek(0x1fb104,id,sizeof(id))) {
            PongProbeRecord(7,"Novatek ROM",Fail,"DMA transfer/event/fault validation failed",error,irqCount);quarantine();return;
        }
        IOLog("PONG INPUT NATIVE: trim=%02x %02x %02x %02x %02x %02x IRQs=%llu\n",
              id[0],id[1],id[2],id[3],id[4],id[5],irqCount);
        if(id[3]!=0x32 || id[4]!=0x65 || id[5]!=0x03) {
            PongProbeRecord(7,"Novatek ROM",Fail,"actual response does not match NT36532");quarantine();return;
        }
        PongProbeRecord(7,"Novatek ROM",Pass,"NT36532 responded over real GENI/GPI DMA",irqCount);
        uint8_t fw[16]={};
        if(!readNovatek(0x125878,fw,sizeof(fw))) {
            PongProbeRecord(8,"FWINFO transport",Fail,"DMA/event validation failed; buffers retained",error,irqCount);
            quarantine();return;
        }
        IOLog("PONG INPUT NATIVE: FWINFO=%02x %02x %02x %02x (not a touch-ready verdict)\n",fw[0],fw[1],fw[2],fw[3]);
        PongProbeRecord(8,"FWINFO transport",Pass,"data read completed; firmware validity not established");
        if(mode==11 || mode==12) {
            unsigned maxX=0,maxY=0;
            if(!loadFirmware(maxX,maxY)) {
                firmwareSnapshot("fresh native firmware attempt failed; no retry/input");
                PongProbeRecord(7,"native firmware",Fail,"initialization gate failed; no report/input, resources retained");quarantine();return;
            }
            if(mode==12) {if(!runInput(maxX,maxY))quarantine();return;}
            if(!observeReports(maxX,maxY)) {quarantine();return;}
            return;
        }
        PongProbeRecord(15,"native input",Skip,
            "transport trial only; firmware/report integration awaits hardware evidence; no input advertised");
    }
};
static Controller device;
static unsigned attempted;
static void worker(void *,wait_result_t){device.run();}
}
extern "C" void pong_native_input_start()
{
    unsigned mode=0;
    if(!PE_parse_boot_argn("pong_touch_native",&mode,sizeof(mode)) || !mode)return;
    if(__atomic_exchange_n(&ponginput::attempted,1,__ATOMIC_ACQ_REL))return;
    if(mode>12) {
        ponginput::PongProbeRecord(15,"native input",ponginput::Skip,"unsupported stage, no fallback",mode);return;
    }
    ponginput::device.mode=mode;thread_t thread;
    kern_return_t r=kernel_thread_start(ponginput::worker,nullptr,&thread);
    if(r==KERN_SUCCESS)thread_deallocate(thread);
    else ponginput::PongProbeRecord(15,"native input",ponginput::Fail,"worker creation failed",uint32_t(r));
}
extern "C" void pong_native_input_panic_dump(){ponginput::dump();}
