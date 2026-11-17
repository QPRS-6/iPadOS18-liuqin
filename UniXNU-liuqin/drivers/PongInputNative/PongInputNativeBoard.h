/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_INPUT_NATIVE_BOARD_H
#define PONG_INPUT_NATIVE_BOARD_H
#include <stdint.h>
namespace ponginput {
/* SM8475 board facts cross-checked with Linux v6.17 gcc-sm8450.c and
 * sm8450.dtsi at 59413493369901ffc7bae4b2be707e2b0d511673. */
constexpr uint64_t GccPa=0x100000, GpiPa=0x900000, WrapperPa=0x9c0000;
/* DT tlmm-base f000000 plus GPIO tile offset100000, equivalently mainline
 * pinctrl@f100000. All GPIO ctl offsets are pin*1000 from this effective base. */
constexpr uint64_t SePa=0x990000, TlmmPa=0xf100000, SmmuPa=0x15000000;
constexpr unsigned TlmmBytes=0x300000;
constexpr unsigned ReportPin=21, ReportCtl=ReportPin*0x1000;
constexpr unsigned ReportIo=ReportCtl+4, ReportCfg=ReportCtl+8, ReportStatus=ReportCtl+12;
/* pinctrl-sm8450 leaves intr_ack_high=false. pinctrl-msm's
 * msm_ack_intr_status therefore writes ZERO; this is not GPI's W1C IRQ ABI. */
constexpr uint32_t ReportStatusAck=0;
static_assert(TlmmPa+ReportIo==0xf115004,"SM8475 GPIO21 data-ready input");
static_assert(ReportStatus+4<=TlmmBytes,"owned report pin fits real TLMM span");
static_assert(TlmmPa+16*0x1000==0xf110000,"SM8475 GPIO16 ctl, not f010000");
static_assert(TlmmPa+19*0x1000==0xf113000,"SM8475 GPIO19 CS ctl");
static_assert(19*0x1000+4<=TlmmBytes,"QUP4 pins fit DT TLMM resource");
/* ID1=6000004b implements 2*128*4K=1MiB; NativeUfs maps this same
 * real APPS window. The Linux DT's shorter e0000 probe span is insufficient
 * for the generic programming API: map the real span, never fake its size. */
constexpr unsigned GpiBytes=0x60000, SmmuBytes=0x100000, AppsInstances=0x7e;
constexpr uint16_t RingSid=0x5b6, DataSid=0x5a3;
constexpr unsigned SeId=4, GpiSpiProtocol=1, GpiIrqBase=244+32;
constexpr unsigned ClockVote=0x62008, WrapperClockMask=0x3c0;
constexpr unsigned SeClockBit=1u<<14, SeHalt=0x274dc, SeRcg=0x274e4;
/* SE4 RCG: parent_map0 TCXO selector0, HID width5, integer mode and HW
 * clock control. Linux's 19.2MHz/div1 table entry encodes HID divider1. */
constexpr uint32_t SeRcgRateMask=0x371f, SeRcgControlMask=SeRcgRateMask|(1u<<20);
constexpr uint32_t SeRcgTcxoConfig=(1u<<20)|1;
static_assert((SeRcgTcxoConfig&SeRcgRateMask)==1,"SE4 TCXO/div1 encoding");
/* gcc-sm8450.c: SE0..6 are voted branches without a HWCG field. The vote
 * alone does not establish a live SE register interface. */
constexpr unsigned SeBranchHalts[]={0x2700c,0x27140,0x27274,0x273a8,0x274dc,0x27610,0x27744};
constexpr unsigned SeRegisterBytes=0x4000, SeDmaModeOffset=0x258;
constexpr uint64_t wrapperSePa(unsigned n){return UINT64_C(0x980000)+n*UINT64_C(0x4000);}
static_assert(wrapperSePa(1)==0x984000 && wrapperSePa(5)==0x994000,"shared SE board addresses");
static_assert(wrapperSePa(SeId)==SePa && SeBranchHalts[SeId]==SeHalt,"SE4 clock/address pairing");
/* Only master/slave AHB branches advertise HWCG bit1. Core branches do not:
 * gcc-sm8450.c and clk-branch.c v6.17 keep those HALT checks mandatory. */
struct WrapperClockBranch {unsigned halt,vote,hwcg;};
constexpr WrapperClockBranch WrapperClocks[]={
    {0x27004,1u<<6,1u<<1},{0x27008,1u<<7,1u<<1},
    {0x33000,1u<<8,0},{0x3300c,1u<<9,0}
};
constexpr bool wrapperClockReady(const WrapperClockBranch &branch,uint32_t vote,uint32_t status) {
    return (vote&branch.vote)==branch.vote &&
        ((status&branch.hwcg)!=0 || (status&(1u<<31))==0);
}
static_assert(wrapperClockReady(WrapperClocks[1],1u<<7,0x80000002),"AHB HWCG idle may halt");
static_assert(!wrapperClockReady(WrapperClocks[1],0,0x80000002),"HWCG cannot replace a vote");
static_assert(!wrapperClockReady(WrapperClocks[1],1u<<7,0x80000000),"ungated AHB halt must fail");
static_assert(!wrapperClockReady(WrapperClocks[2],1u<<8,0x80000002),"core has no HWCG exemption");
constexpr uint32_t DmaIova=0x10000000;
constexpr unsigned DmaBytes=0x10000, TableBytes=0x8000;
constexpr unsigned TxRingOff=0, RxRingOff=0x1000, EventRingOff=0x2000;
constexpr unsigned TxDataOff=0x4000, RxDataOff=0x8000;
constexpr unsigned TransferEntries=64, EventEntries=256, ElementBytes=16;
constexpr unsigned Context0=0, RingLength=4, RingBaseLow=8, RingBaseHigh=12;
constexpr unsigned RingReadLow=16, RingReadHigh=20;
constexpr unsigned GpiStride=0x4000;
/* gpi.c selects ee_offset=0x10000 for sm8450/sm6350 and sets every
 * gpii->regs=ee_base=resource_start-ee_offset. Apply that displacement to
 * offsets, not the mapped pointer: never access below the DT resource. */
constexpr unsigned GpiEeOffset=0x10000;
constexpr unsigned channel(unsigned n,unsigned c){return 0x20000-GpiEeOffset+n*GpiStride+c*0x80;}
constexpr unsigned event(unsigned n){return 0x21000-GpiEeOffset+n*GpiStride;}
constexpr unsigned control(unsigned n,unsigned off){return off-GpiEeOffset+n*GpiStride;}
constexpr unsigned doorbell(unsigned n,unsigned c){return 0x22000-GpiEeOffset+n*GpiStride+c*8;}
constexpr unsigned eventDoorbell(unsigned n){return 0x22100-GpiEeOffset+n*GpiStride;}
static_assert(GpiPa+channel(1,0)==0x914000,"SM8475 APPS channel context");
static_assert(GpiPa+event(1)==0x915000,"SM8475 APPS event context");
static_assert(GpiPa+doorbell(1,0)==0x916000,"SM8475 APPS TX doorbell");
static_assert(GpiPa+eventDoorbell(1)==0x916100,"SM8475 APPS event doorbell");
static_assert(GpiPa+control(1,0x23008)==0x917008,"SM8475 APPS channel command");
static_assert(GpiPa+control(1,0x23080)==0x917080,"SM8475 APPS IRQ status");
static_assert(control(6,0x23404)+4<=GpiBytes,"APPS registers fit DT resource");
constexpr unsigned PhotoGpiInstance=1, PhotoGpiReadOffset=channel(PhotoGpiInstance,0);
static_assert((AppsInstances&(1u<<PhotoGpiInstance))!=0,"photo probe is APPS, not reserved instance0");
static_assert((PhotoGpiReadOffset&3)==0 && PhotoGpiReadOffset+4<=GpiBytes,"single read fits mapped resource");
constexpr unsigned Chain=1, Ieob=1u<<8, Ieot=1u<<9, Link=1u<<11;
struct Element {uint32_t word[4];};
static_assert(sizeof(Element)==ElementBytes,"GPI element ABI");
}
#endif
