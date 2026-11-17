/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* liuqin QCA6490/WCN6855 UART20 bring-up. FIFO transport only, no GPIO bitbang.
 * No Apple controller or keyboard is fabricated. A real HCI reply is required.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOService.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSString.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <libkern/libkern.h>
#include "PongBluetoothProtocol.h"
#include "PongBluetoothDiscovery.h"
#include "PongBluetoothClocks.h"
#ifdef PONG_BLUETOOTH_NATIVE
#include <sys/errno.h>
#include "PongBluetoothNative.h"
#include "PongBluetoothNativeProtocol.h"
#ifdef PONG_BLUETOOTH_USERSPACE
#include "PongBluetoothAddress.h"
extern "C" int pong_bt_runtime_ready(void);
#endif
#endif
namespace pongbt {
enum Result : unsigned {Pass=1,Fail=2,Skip=3,Running=4};
struct Probe {unsigned sequence=0,result=0;const char *stage=nullptr,*reason=nullptr;uint64_t a=0,b=0;};
static Probe probes[8];
static void PongProbeRecord(unsigned slot,const char *stage,Result result,const char *reason,uint64_t a=0,uint64_t b=0) {
    Probe &p=probes[slot];__atomic_add_fetch(&p.sequence,1,__ATOMIC_ACQ_REL);
    p.stage=stage;p.reason=reason;p.a=a;p.b=b;p.result=result;
    __atomic_add_fetch(&p.sequence,1,__ATOMIC_RELEASE);
    IOLog("Pong Bluetooth75: %s %s %s a=%llx b=%llx\n",stage,
        result==Pass?"PASS":result==Fail?"FAIL":result==Skip?"SKIP":"RUNNING",reason,a,b);
}
struct Window {
    IOMemoryMap *map=nullptr;uintptr_t base=0;
    bool open(uint64_t pa,unsigned bytes) {
        auto memory=IODeviceMemory::withRange(pa,bytes);if(!memory)return false;
        map=memory->map(kIOMapAnywhere|kIOMapInhibitCache);memory->release();
        base=map?map->getVirtualAddress():0;return base!=0;
    }
    uint32_t r(unsigned o) const {uint32_t x=*reinterpret_cast<volatile uint32_t *>(base+o);__asm__ volatile("dmb oshld":::"memory");return x;}
    void w(unsigned o,uint32_t x) {__asm__ volatile("dsb sy":::"memory");*reinterpret_cast<volatile uint32_t *>(base+o)=x;__asm__ volatile("dsb sy":::"memory");}
};
static uint64_t now(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000000;}
struct Transport {
    Window gcc,tlmm,wrapper,se,pmic,config,observer;Parser parser;
    uint32_t pinCfg[5]={},pinIo[5]={},oldVote=0,oldRcg[6]={};unsigned rxDepth=0,txDepth=0;
    bool pinsOwned=false,clocksOwned=false,rcgChanged=false,rxStarted=false;unsigned channels=0,received=0,events=0;
#ifdef PONG_BLUETOOTH_NATIVE
    enum {QueueCapacity=64};
#else
    enum {QueueCapacity=8};
#endif
    Packet queued[QueueCapacity];unsigned queueRead=0,queueWrite=0;
#ifdef PONG_BLUETOOTH_NATIVE
    uint8_t addressWire[6]={};unsigned hciVersion=0,lmpVersion=0,manufacturer=0,subversion=0;
    uint32_t nativeRequest=0,nativePending=0,nativeFatal=0;bool nativeMode=false;
    Parser nativeTxParser;uint8_t nativeTxInput[128]={},nativeRxFrame[1029]={};unsigned nativeTxAt=0,nativeTxBytes=0,nativeRxAt=0,nativeRxBytes=0;
    unsigned nativeTxPackets=0,nativeRxPackets=0,nativeRxErrors=0,ibsWake=0,ibsSleep=0;
    unsigned nativeHciTraces=0,controllerSessions=0;
    /* Owner104: the daemon initiates with Filter_Policy1 (white list) and a
     * zero peer address. Cache the last real0x2011 entry in the transport so a
     * direct-address create can be formed, and never log its bytes. */
    uint8_t whiteListAddress[6]={};uint8_t whiteListType=0;bool whiteListValid=false;
    unsigned pendingExtendedCreate=0,leHostFixTraces=0;
    /* Owner108: per-connection GATT summary and class-of-device evidence. */
    unsigned attServices=0,attSummaries=0;bool attHid=false;unsigned attMtu=0;
    unsigned hidStart=0,hidEnd=0,hidReadTraces=0,hidWriteTraces=0;bool hidRange=false;
    static void nativeWake(void *context){__atomic_store_n(&static_cast<Transport *>(context)->nativePending,1u,__ATOMIC_RELEASE);}
    static int nativePower(void *context,bool enabled) {
        auto *t=static_cast<Transport *>(context);uint32_t expected=0;
        unsigned fatal=__atomic_load_n(&t->nativeFatal,__ATOMIC_ACQUIRE);if(fatal)return int(fatal);
        if(!__atomic_compare_exchange_n(&t->nativeRequest,&expected,enabled?1u:2u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return EBUSY;
        nativeWake(t);return 0;
    }
    static bool directPeerEnabled() {
        static int cached=-1;
        if(cached<0) {
            unsigned value=0;
            cached=PE_parse_boot_argn("pong_bt_direct",&value,sizeof(value))?(value==1?1:0):0;
        }
        return cached==1;
    }
    /* Owner104: rewrite only a white-list initiating command whose peer address
     * is empty, using the last real Add Device To White List entry. The filter
     * policy becomes0 and the cached address/type are copied in place. Never
     * reads or exposes the entry outside this transport. */
    static bool directPeerFromWhiteList(Packet &p,const uint8_t *address,uint8_t type,bool valid) {
        if(!valid || p.type!=H4Command || p.bytes!=28 || p.data[2]!=25)return false;
        if((p.data[0]|unsigned(p.data[1])<<8)!=0x200d)return false;
        if(p.data[7]!=1)return false;
        for(unsigned i=0;i<6;i++)if(p.data[9+i])return false;
        p.data[7]=0;p.data[8]=type;
        for(unsigned i=0;i<6;i++)p.data[9+i]=address[i];
        return true;
    }
    static bool legacyCreateTranslationEnabled() {
        static int cached=-1;
        if(cached<0) {
            unsigned value=1;
            cached=PE_parse_boot_argn("pong_bt_lext",&value,sizeof(value))?(value==1?1:0):1;
        }
        return cached==1;
    }
    /* Owner105, after cotunjr/hakios-mayfly qcabt shim0338: this QCA ROM answers
     * legacy0x200d with0x0c Command Disallowed. Translate the identical request
     * into0x2043 Extended_Create_Connection with one1M PHY, and rewrite the
     * controller's answer opcode back so bluetoothd's state machine is intact.
     * Address bytes are copied only inside the transport and never logged. */
    static bool translateLegacyCreate(Packet &p) {
        if(p.type!=H4Command || p.bytes!=28 || p.data[2]!=25)return false;
        if((p.data[0]|unsigned(p.data[1])<<8)!=0x200d)return false;
        const uint8_t *l=p.data+3;uint8_t e[26];
        e[0]=l[4];e[1]=l[12];e[2]=l[5];
        for(unsigned i=0;i<6;i++)e[3+i]=l[6+i];
        e[9]=0x01;
        for(unsigned i=0;i<4;i++)e[10+i]=l[i];
        for(unsigned i=0;i<12;i++)e[14+i]=l[13+i];
        p.data[0]=0x43;p.data[1]=0x20;p.data[2]=26;p.bytes=29;
        for(unsigned i=0;i<26;i++)p.data[3+i]=e[i];
        return true;
    }
    /* Owner113:0x2043 makes the controller report the LE Enhanced Connection
     * Complete subevent0x0a. The host issued legacy0x200d, so deliver the
     * legacy0x01 layout it is waiting for: status, handle, role, address type,
     * address, interval, latency, timeout, clock accuracy. The six-byte peer
     * address is copied in place and never logged. */
    static bool translateEnhancedConnection(Packet &p) {
        if(p.type!=H4Event || p.bytes<33 || p.data[0]!=0x3e || p.data[2]!=0x0a)return false;
        uint8_t out[18];
        for(unsigned i=0;i<11;i++)out[i]=p.data[3+i];
        out[11]=p.data[26];out[12]=p.data[27];
        out[13]=p.data[28];out[14]=p.data[29];
        out[15]=p.data[30];out[16]=p.data[31];
        out[17]=p.data[32];
        p.data[1]=19;p.data[2]=0x01;p.bytes=21;
        for(unsigned i=0;i<18;i++)p.data[3+i]=out[i];
        return true;
    }
    static bool rewriteExtendedCreateResponse(Packet &p) {
        if(p.type!=H4Event)return false;
        if(p.bytes>=7 && p.data[0]==0x0f && p.data[1]==4) {
            if((p.data[4]|unsigned(p.data[5])<<8)!=0x2043)return false;
            p.data[4]=0x0d;p.data[5]=0x20;return true;
        }
        if(p.bytes>=7 && p.data[0]==0x0e && p.data[1]+2u==p.bytes) {
            if((p.data[3]|unsigned(p.data[4])<<8)!=0x2043)return false;
            p.data[3]=0x0d;p.data[4]=0x20;return true;
        }
        return false;
    }
    static uint16_t hciRd16(const uint8_t *at){return uint16_t(at[0]|unsigned(at[1])<<8);}
    static void hciWr16(uint8_t *at,unsigned value){at[0]=uint8_t(value);at[1]=uint8_t(value>>8);}
    static bool normalizeInitiateEnabled() {
        static int cached=-1;
        if(cached<0) {
            unsigned value=0;
            cached=PE_parse_boot_argn("pong_bt_init",&value,sizeof(value)) && value==1?1:0;
        }
        return cached==1;
    }
    /* Owner102: the controller rejects the legacy initiating command with
     * Invalid Parameters. Rewrite only values that the Bluetooth Core
     * specification forbids in legacy0x200d; a specification-valid command is
     * never modified, so the default path stays byte exact. This is opt-in via
     * pong_bt_init=1 and is not claimed as verified until a device log shows
     * the accepted command and a connection event. The6 peer address bytes are
     * never read, copied or exposed. */
    bool normalizeLegacyInitiate(Packet &p,bool enabled) {
        if(!enabled || p.type!=H4Command || p.bytes!=28)return false;
        if((p.data[0]|unsigned(p.data[1])<<8)!=0x200d || p.data[2]!=25)return false;
        uint8_t *q=p.data+3;bool changed=false;
        unsigned interval=hciRd16(q),window=hciRd16(q+2);
        if(interval<4 || interval>0x4000 || window<4 || window>interval) {
            interval=0x60;window=0x30;changed=true;
        }
        unsigned peer=q[5];
        if(peer==2) {peer=0;changed=true;}
        else if(peer==3) {peer=1;changed=true;}
        if(q[4]>1) {q[4]=0;changed=true;}
        unsigned own=q[12];if(own>3) {own=0;changed=true;}
        unsigned low=hciRd16(q+13),high=hciRd16(q+15),latency=hciRd16(q+17),timeout=hciRd16(q+19);
        if(low<6 || low>0xc80 || high<6 || high>0xc80 || high<low || latency>0x1f3 ||
           timeout<0x0a || timeout>0xc80 || 4u*timeout<=(1u+latency)*high) {
            low=6;high=0x0c;latency=0;timeout=0x1f4;hciWr16(q+21,0);hciWr16(q+23,0);changed=true;
        }
        if(!changed)return false;
        hciWr16(q,interval);hciWr16(q+2,window);q[5]=uint8_t(peer);q[12]=uint8_t(own);
        hciWr16(q+13,low);hciWr16(q+15,high);hciWr16(q+17,latency);hciWr16(q+19,timeout);
        return true;
    }
    void traceHci(const char *direction,const Packet &p) {
        unsigned opcode=0,status=0;const char *kind=nullptr;unsigned length=p.bytes;
        if(p.type==H4Command && p.bytes>=3){opcode=p.data[0]|unsigned(p.data[1])<<8;kind="command";}
        else if(p.type==H4Event && p.bytes>=6 && p.data[0]==0x0e && p.data[1]+2u==p.bytes){opcode=p.data[3]|unsigned(p.data[4])<<8;status=p.data[5];kind="complete";}
        else if(p.type==H4Event && p.bytes==6 && p.data[0]==0x0f && p.data[1]==4){opcode=p.data[4]|unsigned(p.data[5])<<8;status=p.data[2];kind="status";}
        // Connection outcome must be visible: a successful or failed create
        // produces a LE meta event that the command/status trace cannot show.
        // Advertising reports are deliberately not traced here.
        if(p.type==H4Event && p.bytes>=4 && p.data[0]==0x3e && p.data[1]+2u==p.bytes &&
           (p.data[2]==0x01 || p.data[2]==0x03 || p.data[2]==0x04 || p.data[2]==0x0a))
            IOLog("Pong Bluetooth102: LE meta subevent=%x status=%x; no address bytes\n",p.data[2],p.data[3]);
        if(p.type==H4Event && p.bytes>=9 && p.data[0]==0x3e && p.data[1]+2u==p.bytes &&
           (p.data[2]==0x01 || p.data[2]==0x0a) && p.data[3]==0) {
            attServices=0;attSummaries=0;attHid=false;attMtu=0;
            hidRange=false;hidStart=hidEnd=0;hidReadTraces=hidWriteTraces=0;
            IOLog("Pong Bluetooth108: LE connection peerAddressType=%x; GATT counters reset\n",p.data[2]==0x01?p.data[7]:p.data[7]);
        }
        // Owner108: a classic keyboard advertises a keyboard class of device in inquiry.
        if(p.type==H4Event && p.bytes>=8 && (p.data[0]==0x02 || p.data[0]==0x22) && p.data[1]+2u==p.bytes) {
            static unsigned inquiryTraces;
            unsigned count=p.data[2],entry=p.data[0]==0x02?13u:14u;
            for(unsigned i=0;i<count && inquiryTraces<16;i++) {
                unsigned at=3u+i*entry;
                if(at+entry>p.bytes)break;
                unsigned cod=p.data[at+8]|unsigned(p.data[at+9])<<8|unsigned(p.data[at+10])<<16;
                inquiryTraces++;
                IOLog("Pong Bluetooth108: inquiry result class=%06x major=%u minor=%u; no address bytes\n",
                    cod,(cod>>8)&31,(cod>>2)&63);
            }
        }
        // Owner106: the LE link now establishes, so the remaining stall is in the
        // classic security exchange. Record bounded outcome scalars only: never
        // an address, link key, passkey or confirmation value.
        if(p.type==H4Event && p.bytes>=3 && p.data[1]+2u==p.bytes) {
            static unsigned securityTraces,pairingTraces;
            unsigned code=p.data[0];
            if(securityTraces<48) {
                if(code==0x03 && p.bytes>=13) {
                    securityTraces++;
                    IOLog("Pong Bluetooth106: classic ConnectionComplete status=%x linkType=%x encryption=%x\n",
                        p.data[2],p.data[11],p.data[12]);
                } else if(code==0x05 && p.bytes>=6) {
                    securityTraces++;
                    IOLog("Pong Bluetooth106: classic DisconnectComplete status=%x reason=%x\n",p.data[2],p.data[5]);
                } else if(code==0x06) {
                    securityTraces++;
                    IOLog("Pong Bluetooth106: AuthenticationComplete status=%x\n",p.data[2]);
                } else if(code==0x08 && p.bytes>=6) {
                    securityTraces++;
                    IOLog("Pong Bluetooth106: EncryptionChange status=%x enabled=%x\n",p.data[2],p.data[5]);
                } else if(code==0x07 && p.bytes>=7) {
                    securityTraces++;
                    IOLog("Pong Bluetooth106: RemoteNameRequestComplete status=%x; no name/address bytes\n",p.data[2]);
                } else if(code==0x33) {
                    securityTraces++;
                    IOLog("Pong Bluetooth106: SimplePairingComplete status=%x\n",p.data[2]);
                } else if(code==0x2b || code==0x2c || code==0x2d || code==0x2e || code==0x2f ||
                          code==0x3b || code==0x3c || code==0x3d) {
                    if(pairingTraces++<12)
                        IOLog("Pong Bluetooth106: pairing event code=%x; no address/key/passkey bytes\n",code);
                }
            }
        }
        // Owner107: does any advertisement carry the HID-over-GATT service UUID?
        if(p.type==H4Event && p.bytes>=8 && p.data[0]==0x3e && (p.data[2]==0x02 || p.data[2]==0x0d)) {
            static unsigned adTraces;
            if(adTraces<8) {
                bool hid=false;
                for(unsigned i=6;i+2<p.bytes;i++)
                    if((p.data[i]==0x02 || p.data[i]==0x03) && p.data[i+1]==0x12 && p.data[i+2]==0x18)hid=true;
                adTraces++;
                IOLog("Pong Bluetooth107: advertising report %u advertisesHID=%u; no address/name bytes\n",adTraces,hid?1:0);
            }
        }
        // L2CAP channel ids show whether ATT4 or SMP6 traffic actually flows.
        if(p.type==H4Acl && p.bytes>=8) {
            static unsigned aclSeen[16][2];
            unsigned cid=p.data[6]|unsigned(p.data[7])<<8;
            unsigned slot=cid<16?cid:15;
            unsigned dir=direction && direction[0]=='R'?1:0;
            if(aclSeen[slot][dir]++<6) {
                IOLog("Pong Bluetooth106: ACL %s cid=%x payload=%u; no data bytes\n",direction,cid,
                    p.data[2]|unsigned(p.data[3])<<8);
                /* Owner107: ATT/SMP opcode only; never a value, key or HID report. */
                if((cid==4 || cid==6) && p.bytes>=9) {
                    unsigned op=p.data[8];
                    if(cid==4 && op==0x01 && p.bytes>=13)
                        IOLog("Pong Bluetooth107: ATT error request=%x handle=%x error=%x\n",
                            p.data[9],p.data[10]|unsigned(p.data[11])<<8,p.data[12]);
                    else IOLog("Pong Bluetooth107: %s op=%x\n",cid==4?"ATT":"SMP",op);
                }
            }
            /* Owner109: does the host actually read the HID service and enable
             * notifications? Handles and opcodes only, never report data. */
            if(cid==4 && p.bytes>=11 && hidRange) {
                unsigned op=p.data[8];
                unsigned handle=p.data[9]|unsigned(p.data[10])<<8;
                if(op==0x0a && handle>=hidStart && handle<=hidEnd && hidReadTraces++<24)
                    IOLog("Pong Bluetooth109: HID read request handle=%x\n",handle);
                if((op==0x12 || op==0x52) && handle>=hidStart && handle<=hidEnd && hidWriteTraces++<8)
                    IOLog("Pong Bluetooth109: HID write handle=%x\n",handle);
                if(op==0x01 && p.bytes>=13 && p.data[12])
                    IOLog("Pong Bluetooth109: ATT error request=%x handle=%x error=%x\n",
                        p.data[9],p.data[10]|unsigned(p.data[11])<<8,p.data[12]);
            }
            /* Owner114: report-map blob reads and HID notifications. Lengths and
             * handles only, never report or key content. */
            if(cid==4 && p.bytes>=9) {
                static unsigned blobTraces,noteTraces;
                unsigned op=p.data[8];
                if(op==0x0c && p.bytes>=13 && blobTraces++<12)
                    IOLog("Pong Bluetooth114: read blob handle=%x offset=%x\n",
                        p.data[9]|unsigned(p.data[10])<<8,p.data[11]|unsigned(p.data[12])<<8);
                if(op==0x0d && blobTraces++<12)
                    IOLog("Pong Bluetooth114: read blob response len=%u\n",p.data[2]|unsigned(p.data[3])<<8);
                if(op==0x1b && p.bytes>=11 && noteTraces++<12)
                    IOLog("Pong Bluetooth114: notification handle=%x length=%u; no report bytes\n",
                        p.data[9]|unsigned(p.data[10])<<8,(p.data[4]|unsigned(p.data[5])<<8)-3u);
            }
            /* Owner117: list the characteristic UUIDs of the HID service so a
             * missing HID Information(2a4a) or Report Map(2a4b) is visible. */
            if(cid==4 && p.bytes>=11 && p.data[8]==0x09 && p.data[9]==7) {
                static unsigned charTraces;
                char list[96];unsigned used=0,at=10u;
                while(at+7<=p.bytes && used+5<sizeof(list)) {
                    unsigned uuid=p.data[at+5]|unsigned(p.data[at+6])<<8;
                    const char digits[]="0123456789abcdef";
                    if(used)list[used++]=',';
                    list[used++]=digits[(uuid>>12)&15];list[used++]=digits[(uuid>>8)&15];
                    list[used++]=digits[(uuid>>4)&15];list[used++]=digits[uuid&15];
                    at+=7;
                }
                list[used]=0;
                if(used && charTraces++<12)
                    IOLog("Pong Bluetooth117: characteristic uuids=%s\n",list);
            }
            /* Service parsing runs for every ATT response, not only the logged ones. */
            if(cid==4 && p.bytes>=9) {
                unsigned op=p.data[8];
                if(op==0x03 && p.bytes>=11)attMtu=p.data[9]|unsigned(p.data[10])<<8;
                if((op==0x09 || op==0x11) && p.bytes>=11) {
                    unsigned entry=p.data[9],at=10u;
                    while(entry>=6 && at+entry<=p.bytes) {
                        unsigned uuid=p.data[at+entry-2]|unsigned(p.data[at+entry-1])<<8;
                        attServices++;
                        if(uuid==0x1812) {
                            attHid=true;
                            if(!hidRange && entry>=6) {
                                hidStart=p.data[at]|unsigned(p.data[at+1])<<8;
                                hidEnd=p.data[at+2]|unsigned(p.data[at+3])<<8;
                                hidRange=true;
                                IOLog("Pong Bluetooth109: HID service handles=%x-%x\n",hidStart,hidEnd);
                            }
                        }
                        at+=entry;
                    }
                    if(attSummaries++<8)
                        IOLog("Pong Bluetooth108: GATT services=%u hid=%u mtu=%u; uuids only\n",
                            attServices,attHid?1:0,attMtu);
                }
            }
        }
        if(!kind)return;
        // Owner102: the fixed512-packet budget was exhausted long before the
        // connect phase, hiding the rejected initiating command. Keep the same
        // general budget but always trace the connect/capability evidence.
        const bool connectTrace=p.type==H4Command &&
            (opcode==0x200d || opcode==0x2043 || opcode==0x2013 || opcode==0x2016 ||
             opcode==0x2019 || opcode==0x201a || opcode==0x201b || opcode==0x2041 || opcode==0x2042 ||
             opcode==0x0405 || opcode==0x040b);
        const bool capabilityTrace=p.type==H4Event && (opcode==0x2003 || opcode==0x201c || opcode==0x200f);
        if(!connectTrace && !capabilityTrace && nativeHciTraces>=512)return;
        nativeHciTraces++;
        IOLog("Pong Bluetooth83: HCI %s session=%u %s opcode=%x len=%u status=%u; no payload/address/key data\n",direction,controllerSessions,kind,opcode,length,status);
        // Targeted, non-sensitive diagnostics to localize the legacy scan
        // blocker: legacy scan params bytes and the LE extended-scan capability
        // byte from Read Local Supported Commands. No address/key/name bytes.
        // Packet::data excludes the H4 type: command parameters start at3.
        // Require the complete declared shape; never read a stale tail byte.
        if(p.type==H4Command && opcode==0x200b && p.bytes==10 && p.data[2]==7)
            IOLog("Pong Bluetooth88: LE SetScanParams type=%x interval=%x window=%x ownAddr=%x filter=%x\n",p.data[3],p.data[4]|unsigned(p.data[5])<<8,p.data[6]|unsigned(p.data[7])<<8,p.data[8],p.data[9]);
        if(p.type==H4Event && opcode==0x1002 && p.data[0]==0x0e && p.bytes==70 && status==0)
            IOLog("Pong Bluetooth88: LE supported-commands[37]=%02x (bit0x20 ext-scan-params, bit0x40 ext-scan-enable)\n",p.data[43]);
        // Classification/discoverability scalars only, to correlate the owner's
        // intermittent peer discovery. Never print addresses, names or keys.
        if(p.type==H4Command && opcode==0x0c24 && p.bytes==6 && p.data[2]==3) {
            unsigned cod=p.data[3]|unsigned(p.data[4])<<8|unsigned(p.data[5])<<16;
            IOLog("Pong Bluetooth90: ClassOfDevice=%06x major=%u minor=%u; classification only, not a connection/HID result\n",cod,(cod>>8)&31,(cod>>2)&63);
        }
        if(p.type==H4Command && opcode==0x0c1a && p.bytes==4 && p.data[2]==1)
            IOLog("Pong Bluetooth90: classic ScanEnable=%u; no address/name data or connection claim\n",p.data[3]);
        // Owner102: owner host sends real legacy0x200d twenty-nine times and the
        // controller answers status12 (invalid parameters), while0x2043 is never
        // used. Record only the non-address scalar fields of the initiating
        // command so the exact illegal parameter is identifiable. The6 peer
        // address bytes at data[9..14] are never read or printed.
        if(p.type==H4Command && opcode==0x200d && p.bytes==28 && p.data[2]==25) {
            unsigned scanInterval=p.data[3]|unsigned(p.data[4])<<8,scanWindow=p.data[5]|unsigned(p.data[6])<<8;
            unsigned intervalMin=p.data[16]|unsigned(p.data[17])<<8,intervalMax=p.data[18]|unsigned(p.data[19])<<8;
            unsigned latency=p.data[20]|unsigned(p.data[21])<<8,timeout=p.data[22]|unsigned(p.data[23])<<8;
            unsigned ceMin=p.data[24]|unsigned(p.data[25])<<8,ceMax=p.data[26]|unsigned(p.data[27])<<8;
            IOLog("Pong Bluetooth102: LE CreateConn scan=%x/%x filter=%x peerType=%x ownType=%x interval=%x-%x latency=%x timeout=%x ce=%x-%x; no address bytes\n",
                scanInterval,scanWindow,p.data[7],p.data[8],p.data[15],intervalMin,intervalMax,latency,timeout,ceMin,ceMax);
        }
        if(p.type==H4Command && opcode==0x2042 && p.bytes==9 && p.data[2]==6)
            IOLog("Pong Bluetooth103: LE ExtScanEnable enable=%x duplicates=%x duration=%x period=%x\n",
                p.data[3],p.data[4],hciRd16(p.data+5),hciRd16(p.data+7));
        if(p.type==H4Command && opcode==0x2041 && p.bytes>=9 && p.data[2]>=6)
            IOLog("Pong Bluetooth103: LE ExtScanParams ownType=%x filter=%x phys=%x type0=%x interval0=%x window0=%x\n",
                p.data[3],p.data[4],p.data[5],p.data[6],hciRd16(p.data+7),hciRd16(p.data+9));
        if(p.type==H4Command && opcode==0x200d && p.bytes==28 && p.data[2]==25) {
            unsigned zero=1;
            for(unsigned i=0;i<6;i++)if(p.data[9+i])zero=0;
            IOLog("Pong Bluetooth103: legacy create peerAddressZero=%u\n",zero);
        }
        if(p.type==H4Command && opcode==0x2043 && p.bytes>=29 && p.data[2]>=26) {
            unsigned scanInterval=p.data[3]|unsigned(p.data[4])<<8,scanWindow=p.data[5]|unsigned(p.data[6])<<8;
            unsigned intervalMin=p.data[17]|unsigned(p.data[18])<<8,intervalMax=p.data[19]|unsigned(p.data[20])<<8;
            IOLog("Pong Bluetooth102: LE ExtCreateConn scan=%x/%x filter=%x ownType=%x peerType=%x phys=%x interval1M=%x-%x; no address bytes\n",
                scanInterval,scanWindow,p.data[7],p.data[8],p.data[9],p.data[16],intervalMin,intervalMax);
        }
        // Controller capability scalars that gate the initiating path; no keys.
        if(p.type==H4Event && (opcode==0x2003 || opcode==0x201c) && status==0 &&
           p.data[0]==0x0e && p.data[1]+2u==p.bytes && p.bytes>=14)
            IOLog("Pong Bluetooth102: %s=%02x%02x%02x%02x%02x%02x%02x%02x (data6..13); capability only\n",
                opcode==0x2003?"LE features":"LE supported states",
                p.data[13],p.data[12],p.data[11],p.data[10],p.data[9],p.data[8],p.data[7],p.data[6]);
        if(p.type==H4Event && opcode==0x200f && status==0 && p.data[0]==0x0e && p.data[1]+2u==p.bytes && p.bytes>=7)
            IOLog("Pong Bluetooth102: LE white list size=%u; capability only\n",p.data[6]);

    }
#endif
    bool wait(const Window &w,unsigned o,uint32_t mask,uint32_t want,unsigned ms) {
        uint64_t end=now()+ms;for(unsigned n=0;n<=ms;n++){if((w.r(o)&mask)==want)return true;if(now()>=end)break;IOSleep(1);}return false;
    }
    // Read-only PMIC observer: same real rails already held by working WiFi67.
    bool railRead(unsigned channel,unsigned address,uint8_t *out,unsigned bytes,uint32_t &status) {
        unsigned off=channel*32;
        observer.w(off,(1u<<27)|(address<<4)|(bytes-1));
        uint64_t end=now()+2;
        for(unsigned tries=0;tries<2000;tries++) {
            status=observer.r(off+8);
            if(status&1)break;
            if(now()>=end)break;
        }
        if((status&15)!=1)return false;
        uint32_t word=observer.r(off+0x18);
        for(unsigned i=0;i<bytes;i++)out[i]=uint8_t(word>>(8*i));
        return true;
    }
    bool supplies() {
        if(!pmic.open(0xc400000,0x3000)||!config.open(0xc42d000,0x4000))return false;
        channels=pmic.r(4)&0x7ff;if(channels<1 || channels>1024 || (pmic.r(0)&0xf0000000)!=0x70000000)return false;
        if(!observer.open(0xc440000,(channels*32+4095)&~4095u))return false;
        const struct {unsigned ppid,mv,type,subtype;} rails[]={
            {0x49e,1012,28,14},{0x1b9,966,3,10},{0x1b6,1800,3,10},{0x29b,1900,3,10},{0x1bc,1350,3,10}};
        for(const auto &rail:rails) {
            int selected=-1;
            for(unsigned i=0;i<channels;i++) {
                unsigned map=pmic.r(0x2000+4*i);
                if(!map || ((map>>8)&0xfff)!=rail.ppid)continue;
                if(selected<0 || (config.r(4*i)&7)==0)selected=int(i);
            }
            if(selected<0){IOLog("Pong Bluetooth76: PPID=%x no observer channel\n",rail.ppid);return false;}
            uint64_t end=now()+100;uint32_t status=0;unsigned failures=0;bool ready=false;
            uint8_t identity[2]={},state=0,enabled=0,voltage[2]={};unsigned mv=0;
            do {
                bool read=railRead(unsigned(selected),4,identity,2,status) &&
                    railRead(unsigned(selected),8,&state,1,status) &&
                    railRead(unsigned(selected),0x46,&enabled,1,status) &&
                    railRead(unsigned(selected),0x40,voltage,2,status);
                if(read) {
                    mv=unsigned(voltage[0])|unsigned(voltage[1])<<8;
                    ready=identity[0]==rail.type && identity[1]==rail.subtype &&
                        (state&0x80) && (enabled&0x80) && mv>=rail.mv;
                    if(ready)break;
                } else failures++;
                IOSleep(1);
            }while(now()<end);
            IOLog("Pong Bluetooth76: PPID=%x channel=%u owner=%u type=%x/%x mv=%u status=%x enable=%x observer=%x retries=%u %s\n",
                rail.ppid,unsigned(selected),config.r(4*unsigned(selected))&7,identity[0],identity[1],mv,state,enabled,status,failures,ready?"PASS":"FAIL");
            if(!ready)return false;
        }
        return true;
    }
    void pin(unsigned p,unsigned mux,int level) {
        unsigned o=p*0x1000;uint32_t old=tlmm.r(o),cfg=(old&~0x3ffu)|(mux<<2);
        if(old&(1u<<11))cfg|=1u<<12;
        if(level>=0){cfg|=1u|(7u<<6)|(1u<<9);tlmm.w(o+4,level?2:0);}
        tlmm.w(o,cfg);if(level>=0)tlmm.w(o+4,level?2:0);
    }
    void clockSnapshot(const char *phase) {
        if(!gcc.base)return;
        uint32_t l=gcc.r(0x10),alpha=gcc.r(0x14),user=gcc.r(0x18);
        IOLog("Pong Bluetooth78: clock %s GPLL0 mode=%x opmode=%x L=%x alpha=%x user=%x mainHz=%llu pllVote=%x; shared PLL not written\n",
            phase,gcc.r(0),gcc.r(4),l,alpha,user,gpll0MainHz(l,alpha),gcc.r(0x62018));
        IOLog("Pong Bluetooth78: clock %s vote=%x cmd=%x cfg=%x M=%x N=%x D=%x dfs=%x AHBm=%x AHBs=%x SE5=%x\n",
            phase,gcc.r(0x62010),gcc.r(0x2e618),gcc.r(0x2e61c),gcc.r(0x2e620),gcc.r(0x2e624),gcc.r(0x2e628),gcc.r(0x2e62c),gcc.r(0x2e004),gcc.r(0x2e008),gcc.r(0x2e610));
    }
    bool clockFailure(const char *reason,unsigned offset,uint32_t value) {
        clockSnapshot("FAIL");
        PongProbeRecord(2,"clock",Fail,reason,0x100000ULL+offset,value);return false;
    }
    bool clockBranch(const ClockBranch &branch) {
        uint64_t end=now()+30;
        for(unsigned i=0;i<=30;i++) {
            if(clockBranchReady(branch,gcc.r(0x62010),gcc.r(branch.offset)))return true;
            if(now()>=end)break;IOSleep(1);
        }
        return false;
    }
    bool clocks() {
        if(!gcc.open(0x100000,0x80000)){PongProbeRecord(2,"clock",Fail,"GCC mapping unavailable");return false;}
        clockSnapshot("entry");
        PongProbeRecord(2,"clock",Running,"bounded actual GPLL0/source/vote/AHB/SE5 prerequisites");
        if(!wait(gcc,0,1u<<31,1u<<31,100))return clockFailure("GPLL0 lock timeout; no shared PLL or UART clock writes",0,gcc.r(0));
        if(!gpll0Even300(gcc.r(0x10),gcc.r(0x14),gcc.r(0x18)))return clockFailure("actual GPLL0/even parent is not 600/300MHz; no PLL reprogramming",0x18,gcc.r(0x18));
        oldVote=gcc.r(0x62010);for(unsigned i=0;i<6;i++)oldRcg[i]=gcc.r(0x2e618+4*i);
        // UART20 alone: parentGPLL0 even300MHz, M384/N15625 =7.3728MHz.
        // Preserve all other wrapper/GPU/WLAN clock votes.
        gcc.w(0x62010,oldVote&~(1u<<9));clocksOwned=true;
        rcgChanged=true;gcc.w(0x2e62c,gcc.r(0x2e62c)&~1u); // this SE's DFS, restored below
        if(!wait(gcc,0x2e62c,1,0,30))return clockFailure("SE5 DFS disable not acknowledged",0x2e62c,gcc.r(0x2e62c));
        uint32_t cfg=gcc.r(0x2e61c);gcc.w(0x2e61c,(cfg&~0x371fu)|(6u<<8)|(2u<<12)|1u);
        gcc.w(0x2e620,384);gcc.w(0x2e624,(~(15625u-384u))&0xffff);gcc.w(0x2e628,(~15625u)&0xffff);
        gcc.w(0x2e618,gcc.r(0x2e618)|1u);
        if(!wait(gcc,0x2e618,1,0,30))return clockFailure("SE5 RCG UPDATE timeout",0x2e618,gcc.r(0x2e618));
        if((gcc.r(0x2e61c)&0x371f)!=0x2601 || (gcc.r(0x2e620)&0xffff)!=384 ||
           (gcc.r(0x2e624)&0xffff)!=((~(15625u-384u))&0xffff) || (gcc.r(0x2e628)&0xffff)!=((~15625u)&0xffff))
            return clockFailure("SE5 clock source/MND readback mismatch",0x2e61c,gcc.r(0x2e61c));
        gcc.w(0x62010,gcc.r(0x62010)|(1u<<9)|(1u<<2)|(1u<<1));
        const uint32_t votes=(1u<<9)|(1u<<2)|(1u<<1);
        if(!wait(gcc,0x62010,votes,votes,30))return clockFailure("UART20/AHB clock vote not acknowledged",0x62010,gcc.r(0x62010));
        const ClockBranch branches[]={{0x2e004,1u<<2,1u<<1},{0x2e008,1u<<1,1u<<1},{0x2e610,1u<<9,0}};
        for(const auto &branch:branches) {
            if(!clockBranch(branch))return clockFailure("clock branch neither voted HWCG-idle nor ON; UART untouched",branch.offset,gcc.r(branch.offset));
        }
        if(!wrapper.open(0x8c0000,0x2000))return clockFailure("QUP2 wrapper mapping unavailable",0x2e004,gcc.r(0x2e004));
        clockSnapshot("ready");return true;
    }
    bool uart() {
        // Do not touch an engine with absent/wrong resident GENI microcode.
        if(!se.open(0x894000,0x4000))return false;
        uint32_t rev=se.r(0x68),version=wrapper.r(4);
        IOLog("Pong Bluetooth75: UART20 firmware=%x wrapperVersion=%x ios=%x\n",rev,version,se.r(0x908));
        if(((rev>>8)&255)!=2 || (se.r(0x64)&1)) {
            PongProbeRecord(3,"GENI UART",Skip,"resident UART microcode/FIFO prerequisite absent",rev,version);return false;
        }
        if(se.r(0x40)&0x1001) {
            se.w(0x604,4);se.w(0x634,4);
            if(!wait(se,0x40,0x1001,0,30))return false;
        }
        txDepth=(se.r(0xe24)>>16)&255;rxDepth=(se.r(0xe28)>>16)&255;
        if(txDepth<4 || txDepth>256 || rxDepth<4 || rxDepth>256 || ((se.r(0xe24)>>24)&63)!=32)return false;
        unsigned div=7372800/(115200*sampling(version));if(div!=2 && div!=4)return false;
        se.w(0x48,1u|(div<<4));se.w(0x4c,1u|(div<<4));se.w(0x7c,0);
        se.w(0x25c,0);se.w(0x2a4,0);se.w(0x280,0);se.w(0x2a8,0);
        se.w(0x268,8);se.w(0x28c,8);se.w(0x26c,0);se.w(0x294,160);
        se.w(0x22c,0);se.w(0x2ac,0); // real RTS/CTS, never mask CTS
        se.w(0x260,packing(0));se.w(0x264,packing(2));se.w(0x284,packing(0));se.w(0x288,packing(2));se.w(0x254,0);
        se.w(0xe18,0);se.w(0xe1c,0);se.w(0x258,se.r(0x258)&~1u);
        se.w(0x28,se.r(0x28)|0x7f);se.w(0xe30,se.r(0xe30)|0xf);se.w(0x24,0x7f);se.w(0x20,1);
        se.w(0x80c,2);se.w(0x810,2);se.w(0x814,rxDepth-2);
        se.w(0x618,~0u);se.w(0x648,~0u);se.w(0x614,0);se.w(0x644,0);
        se.w(0x630,1u<<27);rxStarted=true;
        PongProbeRecord(3,"GENI UART",Pass,"115200 8N1 FIFO with real RTS/CTS; no DMA",txDepth,rxDepth);return true;
    }
    bool send(const uint8_t *data,unsigned bytes) {
        if(!bytes || bytes>1029 || !wait(se,0x40,1,0,100))return false;
        se.w(0x618,~0u);se.w(0x270,bytes);se.w(0x600,1u<<27);
        unsigned at=0;uint64_t end=now()+300;
        while(at<bytes && now()<end) {
            unsigned occupied=se.r(0x800)&0xfffffff;if(occupied>txDepth)return false;
            unsigned available=txDepth-occupied;
            while(available-- && at<bytes){uint32_t word=0;for(unsigned i=0;i<4 && at<bytes;i++)word|=uint32_t(data[at++])<<(8*i);se.w(0x700,word);}
            if(at<bytes)IOSleep(1);
        }
        return at==bytes && wait(se,0x40,1,0,300) && !(se.r(0x610)&0x3c);
    }
    bool pump(Packet &out) {
        if(queueRead!=queueWrite){out=queued[queueRead++%QueueCapacity];return true;}
        uint32_t status=se.r(0x804);unsigned bytes=fifoBytes(status,rxDepth),words=status&0x1ffffff;
        if(words>rxDepth || ((words!=0)&&bytes==0)){
#ifdef PONG_BLUETOOTH_NATIVE
            if(nativeMode)nativeRxErrors++;
#endif
            return false;
        }
        for(unsigned i=0;i<words;i++) {
            uint32_t word=se.r(0x780);unsigned take=bytes>4?4:bytes;bytes-=take;
            for(unsigned n=0;n<take;n++) {
                Packet p;received++;
                if(parser.feed(uint8_t(word>>(8*n)),p)) {
                    if(p.type==IbsWake){uint8_t ack=IbsAck;if(!send(&ack,1)){
#ifdef PONG_BLUETOOTH_NATIVE
                        if(nativeMode)nativeRxErrors++;
#endif
                        return false;
                    }
#ifdef PONG_BLUETOOTH_NATIVE
                        ibsWake++;
#endif
                    }
#ifdef PONG_BLUETOOTH_NATIVE
                    else if(p.type==IbsSleep)ibsSleep++;
                    else if(nativeMode && p.type>=H4Command && p.type<=H4Iso) {
                        if(p.type==H4Command || queueWrite-queueRead>=QueueCapacity){nativeRxErrors++;return false;}
                        queued[queueWrite++%QueueCapacity]=p;if(p.type==H4Event)events++;
                    }
#endif
                    else if(p.type==H4Event){
                        if(queueWrite-queueRead>=QueueCapacity){IOLog("Pong Bluetooth75: event queue overflow; transaction refused\n");return false;}
                        queued[queueWrite++%QueueCapacity]=p;events++;
                    }
                }
            }
        }
        se.w(0x648,(1u<<26)|(1u<<27));se.w(0x618,(1u<<26)|(1u<<27));
        if(queueRead!=queueWrite){out=queued[queueRead++%QueueCapacity];return true;}return false;
    }
    bool queryQcaVersion(Version &version,bool evidence) {
        uint8_t query=0x19,cmd[5];command(cmd,sizeof(cmd),0xfc00,&query,1);
        unsigned sampled=0,start=received;bool sent=send(cmd,sizeof(cmd));
        if(sent) {
            uint64_t end=now()+1500;
            while(now()<end) {
                Packet p;
                if(pump(p)) {
                    if(evidence && sampled<4 && p.type==H4Event && p.bytes>=4 &&
                       (p.data[0]==0xff || (p.data[0]==0x0e && p.bytes>=8 && (p.data[3]|unsigned(p.data[4])<<8)==0xfc00))) {
                        IOLog("Pong Bluetooth79: QCA reply event=%x bytes=%u header=%x/%x/%x; envelope only, no addresses/payload dump\n",p.data[0],p.bytes,
                            p.data[0]==0xff?p.data[2]:p.data[5],p.data[0]==0xff?p.data[3]:p.data[6],p.data[0]==0xff?0:p.data[7]);sampled++;
                    }
                    if(qcaVersion(p,version))return true;
                }
                IOSleep(1);
            }
        }
        if(evidence)IOLog("Pong Bluetooth79: QCA query sent=%u receivedDelta=%u envelopeSamples=%u; unknown identity does not authorize firmware download\n",sent?1:0,received-start,sampled);
        return false;
    }
    bool transact(uint16_t opcode,Packet &reply,const uint8_t *payload=nullptr,unsigned bytes=0) {
        uint8_t data[255];unsigned count=unsigned(command(data,sizeof(data),opcode,payload,bytes));
        if(!count || !send(data,count))return false;
        uint64_t end=now()+1500;
        for(unsigned i=0;i<1500 && now()<end;i++) {
            Packet p;
            if(pump(p)) {uint8_t status=255;
                if(commandComplete(p,opcode,status)){reply=p;if(status)IOLog("Pong Bluetooth75: HCI opcode=%x status=%u\n",opcode,status);return status==0;}
                if(commandStatus(p,opcode,status) && status!=0){IOLog("Pong Bluetooth75: HCI opcode=%x status=%u\n",opcode,status);return false;}
            }
            IOSleep(1);
        }
        IOLog("Pong Bluetooth75: HCI timeout opcode=%x rx=%u events=%u bad=%u m_irq=%x s_irq=%x ios=%x\n",opcode,received,events,parser.bad,se.r(0x610),se.r(0x640),se.r(0x908));return false;
    }
    void discover() {
        PongProbeRecord(7,"discovery",Running,"bounded QCA identity/features/classic/LE; no connections or native HID endpoint");
        Packet reply;Version version;bool gotVersion=queryQcaVersion(version,false);
        if(gotVersion)IOLog("Pong Bluetooth77: QCA actual product=%x soc=%x rom=%x patch=%x; no firmware guessed\n",version.product,version.soc,version.rom,version.patch);
        else IOLog("Pong Bluetooth77: QCA version SKIP no matching EDL reply; stock ROM operation retained\n");
        if(!transact(0x1003,reply) || reply.bytes!=14) {
            PongProbeRecord(7,"discovery",Skip,"complete real controller features required before inquiry/LE scan",reply.bytes);return;
        }
        bool le=(reply.data[10]&0x40)!=0,bredr=(reply.data[10]&0x20)==0;
        IOLog("Pong Bluetooth77: real LMP features LE=%u BR_EDR=%u\n",le?1:0,bredr?1:0);
        // HCI Reset defaults do not enable LE Meta-Event. Explicitly request
        // inquiry-complete/RSSI, command/error and LE reports for this trial.
        // No connection/pairing event is used to fabricate native readiness.
        const uint8_t eventMask[]={1,0xe0,0,0,2,0x40,0,uint8_t(le?0x20:0)};
        if(!transact(0x0c01,reply,eventMask,sizeof(eventMask))) {
            PongProbeRecord(7,"discovery",Skip,"HCI event mask refused; no inquiry or LE scan started");return;
        }
        IOLog("Pong Bluetooth77: HCI event mask PASS inquiry/RSSI and LE_meta=%u\n",le?1:0);
        uint8_t mode=1;
        bool classic=bredr && transact(0x0c45,reply,&mode,1);
        Discovery result;
        if(classic) {
            const uint8_t inquiry[]={0x33,0x8b,0x9e,8,0};uint8_t cmd[9];command(cmd,sizeof(cmd),0x0401,inquiry,5);
            bool accepted=false,completed=false,sent=send(cmd,sizeof(cmd));uint8_t inquiryStatus=255,completeStatus=255;
            if(sent) {
                uint64_t end=now()+15000;
                while(now()<end) {
                    Packet p;
                    if(pump(p)) {
                        uint8_t status=255;if(commandStatus(p,0x0401,status)){inquiryStatus=status;accepted=status==0;if(status)break;}
                        discoveryEvent(p,result);
                        if(p.type==H4Event && p.bytes==3 && p.data[0]==1 && p.data[1]==1){completed=true;completeStatus=p.data[2];break;}
                    }
                    IOSleep(1);
                }
            }
            IOLog("Pong Bluetooth77: classic inquiry accepted=%u commandStatus=%u complete=%u status=%u reports=%u keyboardClass=%u malformed=%u\n",accepted?1:0,inquiryStatus,completed?1:0,completeStatus,result.classic,result.classicKeyboard,result.malformed);
            // Bounded cancellation also quiesces a missing inquiry-complete.
            if(sent && !completed)transact(0x0402,reply);
        } else IOLog("Pong Bluetooth77: classic inquiry SKIP BR_EDR=%u; successful RSSI mode required\n",bredr?1:0);
        if(le) {
            const uint8_t leMask[]={2,0,0,0,0,0,0,0};
            const uint8_t params[]={0,0x10,0,0x10,0,0,0};const uint8_t enable[]={1,1},disable[]={0,1};
            bool masks=transact(0x2001,reply,leMask,sizeof(leMask));
            bool configured=masks && transact(0x200b,reply,params,sizeof(params));
            bool active=configured && transact(0x200c,reply,enable,sizeof(enable));
            if(active) {
                uint64_t end=now()+10000;
                while(now()<end){Packet p;if(pump(p))discoveryEvent(p,result);IOSleep(1);}
            }
            // A lost enable reply is not proof the controller stayed stopped.
            // Quiesce even that uncertain outcome before ownership restoration.
            bool stopped=configured && transact(0x200c,reply,disable,sizeof(disable));
            IOLog("Pong Bluetooth77: LE scan eventMask=%u active=%u stopped=%u reports=%u HIDservice=%u malformed=%u; addresses/names not logged\n",masks?1:0,active?1:0,stopped?1:0,result.le,result.leHid,result.malformed);
        }
        PongProbeRecord(7,"HID discovery",Skip,"real discovery counts only; native Apple controller/pairing/keyboard reports not yet implemented",result.classicKeyboard,result.leHid);
    }
    void restore() {
        if(rxStarted){se.w(0x634,4);if(!wait(se,0x40,1u<<12,0,30))se.w(0x634,2);rxStarted=false;}
        if(pinsOwned){const unsigned pins[]={76,77,78,79,81};for(unsigned i=0;i<5;i++){tlmm.w(pins[i]*0x1000+4,pinIo[i]&2);tlmm.w(pins[i]*0x1000,pinCfg[i]);}pinsOwned=false;}
        if(clocksOwned) {
            gcc.w(0x62010,gcc.r(0x62010)&~(1u<<9));
            bool updated=true;
            if(rcgChanged) {
                gcc.w(0x2e62c,gcc.r(0x2e62c)&~1u);
                for(unsigned i=1;i<5;i++)gcc.w(0x2e618+4*i,oldRcg[i]);
                gcc.w(0x2e618,(oldRcg[0]&~1u)|1u);updated=wait(gcc,0x2e618,1,0,30);
                gcc.w(0x2e62c,oldRcg[5]);rcgChanged=false;
            }
            const uint32_t owned=(1u<<9)|(1u<<2)|(1u<<1);
            gcc.w(0x62010,(gcc.r(0x62010)&~owned)|(oldVote&owned));clocksOwned=false;
            bool votes=(gcc.r(0x62010)&owned)==(oldVote&owned);
            IOLog("Pong Bluetooth78: clock restore sourceUpdate=%u voteRestored=%u; shared PLL never reprogrammed\n",updated?1:0,votes?1:0);
            if(!updated || !votes)clockFailure("clock restoration not acknowledged; no success fabricated",0x2e618,gcc.r(0x2e618));
        }
    }
    bool coldBoundary() {
        // BT_EN has really been held low. Drain only this resident UART's old
        // RX FIFO before asserting the controller, never shared rails/PLL/SPI.
        unsigned drained=0;
        for(unsigned attempt=0;attempt<8;attempt++) {
            unsigned words=se.r(0x804)&0x1ffffff;
            if(words>rxDepth)return false;
            if(!words) {
                se.w(0x648,(1u<<26)|(1u<<27));se.w(0x618,(1u<<26)|(1u<<27));
                parser.beginSession();queueRead=queueWrite=0;
#ifdef PONG_BLUETOOTH_NATIVE
                nativeTxParser.beginSession();nativeTxAt=nativeTxBytes=nativeRxAt=nativeRxBytes=0;
                controllerSessions++;
#endif
                IOLog("Pong Bluetooth83: cold controller boundary drainedWords=%u; new framing session, cumulative failures retained\n",drained);return true;
            }
            for(unsigned i=0;i<words;i++){(void)se.r(0x780);drained++;}
        }
        return false;
    }
    bool settle() {
        /* 83 shows one stray invalid byte on the first Reset after a powered
         * cycle (bad=1), then success after re-init. BT_EN had been low and the
         * FIFO drained, so only post-assert startup bytes remain. Observe a
         * bounded quiet period before the first *real* command; discard counted
         * bytes, never a packet, and never invent a Reset response. */
        unsigned drained=0,quiet=0;
        for(unsigned ms=0;ms<200;ms++) {
            unsigned words=se.r(0x804)&0x1ffffff;
            if(words>rxDepth)return false;
            if(!words) {
                if(++quiet<2){IOSleep(1);continue;}
                se.w(0x648,(1u<<26)|(1u<<27));se.w(0x618,(1u<<26)|(1u<<27));
                parser.beginSession();queueRead=queueWrite=0;
#ifdef PONG_BLUETOOTH_NATIVE
                nativeTxParser.beginSession();nativeTxAt=nativeTxBytes=nativeRxAt=nativeRxBytes=0;
                controllerSessions++;
#endif
                IOLog("Pong Bluetooth84: controller post-assert settle quietMs=%u drainedBytes=%u; real Reset still required\n",quiet,drained);
                return true;
            }
            quiet=0;
            for(unsigned i=0;i<words;i++){(void)se.r(0x780);drained+=4;}
            if(drained>4096)return false;
            IOSleep(1);
        }
        return false;
    }
    bool prepareController() {
        PongProbeRecord(0,"board",Running,"liuqin UART20 894000, CTS/RTS/TX/RX76/77/78/79, BT_EN81; GPU untouched");
        if(!tlmm.open(0xf100000,0xd0000)){PongProbeRecord(0,"board",Fail,"TLMM mapping absent");return false;}
        // Initial trial shares the already verified WiFi67 rail provider.
        uint64_t end=now()+60000;
        while(!(tlmm.r(80*0x1000+4)&1) && now()<end)IOSleep(50);
        if(!(tlmm.r(80*0x1000+4)&1) || !supplies()){PongProbeRecord(1,"power",Skip,"working WLAN/shared physical rails required; no BT_EN or UART MMIO");return false;}
        PongProbeRecord(1,"power",Pass,"five actual shared PMIC rails enabled; WLAN80/XO204 not changed");
        if(!clocks()){restore();return false;}
        PongProbeRecord(2,"clock",Pass,"actual parent/MND/votes and HWCG-aware AHB/strict SE5 ready; other clock votes retained");
        const unsigned pins[]={76,77,78,79,81};for(unsigned i=0;i<5;i++){pinCfg[i]=tlmm.r(pins[i]*0x1000);pinIo[i]=tlmm.r(pins[i]*0x1000+4);}
        pinsOwned=true;for(unsigned i=0;i<4;i++)pin(pins[i],1,-1);
        if(!uart()){PongProbeRecord(3,"UART trial",Skip,"resident UART protocol/FIFO required; transport not fabricated");restore();return false;}
        pin(81,0,0);IOSleep(100);
        if((tlmm.r(81*0x1000+4)&1) || !coldBoundary()){PongProbeRecord(4,"BT cold boundary",Fail,"actual BT_EN low and bounded old-RX drain required before reset");restore();return false;}
        pin(81,0,1);IOSleep(200);
        if(!(tlmm.r(81*0x1000+4)&1)){PongProbeRecord(4,"BT_EN",Fail,"GPIO81 input did not follow request");restore();return false;}
        PongProbeRecord(4,"BT_EN",Pass,"real GPIO81 asserted, WLAN/pen GPIO unchanged");
        if(!settle()){PongProbeRecord(4,"BT settle",Fail,"bounded post-assert RX quiet/drain required before the first real command");restore();return false;}
        Packet reply;
        if(!transact(0x0c03,reply)){PongProbeRecord(5,"HCI Reset",Fail,"no matching successful controller response");restore();return false;}
        PongProbeRecord(5,"HCI Reset",Pass,"real command-complete status0");
        if(!transact(0x1001,reply) || reply.bytes!=14){PongProbeRecord(6,"HCI version",Fail,"no complete local-version response");restore();return false;}
        IOLog("Pong Bluetooth75: real version hci=%u revision=%x lmp=%u manufacturer=%x subversion=%x\n",reply.data[6],reply.data[7]|reply.data[8]<<8,reply.data[9],reply.data[10]|reply.data[11]<<8,reply.data[12]|reply.data[13]<<8);
#ifdef PONG_BLUETOOTH_NATIVE
        hciVersion=reply.data[6];lmpVersion=reply.data[9];manufacturer=reply.data[10]|reply.data[11]<<8;subversion=reply.data[12]|reply.data[13]<<8;
#endif
        if(!transact(0x1009,reply) || reply.bytes!=12){PongProbeRecord(6,"HCI address",Fail,"no complete controller address response");restore();return false;}
#ifdef PONG_BLUETOOTH_NATIVE
        for(unsigned i=0;i<6;i++)addressWire[i]=reply.data[6+i];
#endif
        PongProbeRecord(6,"HCI identity",Pass,"real version and address replies validated; address bytes not printed",received,events);
        return true;
    }
#ifdef PONG_BLUETOOTH_NATIVE
    bool configureOwnerAddress(const Version &version) {
#ifdef PONG_BLUETOOTH_USERSPACE
        if(version.product!=0x13 || version.soc!=0x400c0210 || version.rom!=0x0201 || manufacturer!=0x1d) {
            PongProbeRecord(7,"native identity",Fail,"actual controller differs from owner80 QCA identity; no address write");return false;
        }
        uint8_t expected[6];for(unsigned i=0;i<6;i++)expected[i]=pong_bt_owner_address[5-i];
        if(!memcmp(addressWire,expected,6))return true;
        uint8_t commandBytes[10];command(commandBytes,sizeof(commandBytes),0xfc14,pong_bt_owner_address,6);
        if(!send(commandBytes,sizeof(commandBytes)))return false;
        bool replied=false;uint64_t end=now()+1500;
        while(now()<end){Packet p;if(pump(p)){
            uint8_t status=255;
            if(commandComplete(p,0xfc14,status)){if(status)return false;replied=true;break;}
            if(commandStatus(p,0xfc14,status) && status)return false;
            if(p.type==H4Event && p.bytes>=2 && p.data[0]==0xff){replied=true;break;}
        }IOSleep(1);}
        if(!replied)return false;
        Packet readback;if(!transact(0x1009,readback)||readback.bytes!=12)return false;
        if(memcmp(readback.data+6,expected,6))return false;
        memcpy(addressWire,expected,6);
        IOLog("Pong Bluetooth81: owner-provided original address configured and actual HCI readback matched; address not logged\n");
#else
        (void)version;
#endif
        return true;
    }
    bool nativeOpen() {
        if(rxDepth*4/3+1>QueueCapacity){PongProbeRecord(7,"native UART.SK",Fail,"actual FIFO exceeds bounded native packet queue; no depth guessed");return false;}
        Version version;
        bool identified=queryQcaVersion(version,true);
        if(identified)IOLog("Pong Bluetooth79: QCA actual product=%x soc=%x rom=%x patch=%x; no firmware guessed\n",version.product,version.soc,version.rom,version.patch);
#ifdef PONG_BLUETOOTH_USERSPACE
        uint64_t runtimeEnd=now()+60000;
        while(!pong_bt_runtime_ready() && now()<runtimeEnd)IOSleep(20);
        if(!pong_bt_runtime_ready()){PongProbeRecord(7,"native userspace",Skip,"matching22H374 runtime not installed; no early node publication");return false;}
        if(!identified || !configureOwnerAddress(version)){PongProbeRecord(7,"native identity",Fail,"validated owner controller/address write-readback required; no identity guessed");return false;}
#endif
        uint8_t network[6];
        if(!nativeAddress(addressWire,network)){PongProbeRecord(7,"native UART.SK",Fail,"actual controller address empty/invalid; no MAC guessed or node published");return false;}
        Packet reply;
        if(!transact(0x1003,reply) || reply.bytes!=14){PongProbeRecord(7,"native UART.SK",Fail,"actual LMP feature response required before native handoff");return false;}
        IOLog("Pong Bluetooth79: native actual LMP LE=%u BR_EDR=%u; no forced capabilities\n",(reply.data[10]&0x40)?1:0,(reply.data[10]&0x20)?0:1);
        char firmware[64];snprintf(firmware,sizeof(firmware),"resident-HCI%u-LMP%u-M%02x-S%04x",hciVersion,lmpVersion,manufacturer,subversion);
        nativeMode=true;queueRead=queueWrite=0;parser.reset();nativeTxParser.reset();
        nativeTxAt=nativeTxBytes=nativeRxAt=nativeRxBytes=0;
        qbt_transport_boot_result(0,firmware,115200);
        int status=pong_bluetooth_native_status();
        if(status){nativeMode=false;pong_bluetooth_native_ready(false);PongProbeRecord(7,"native UART.SK",Fail,"kernel control registration failed; no ready fabricated",status);return false;}
        if(!pong_bluetooth_native_publish(network,115200)) {
            nativeMode=false;qbt_transport_power_result(false,ENODEV,nullptr,115200);
            PongProbeRecord(7,"native UART.SK",Fail,"real node publication failed/existing identity retained");return false;
        }
        pong_bluetooth_native_ready(true);
        PongProbeRecord(7,"native UART.SK",Running,"real H4 controller-backed pipe; Apple bootstrap/channel/pairing/HID still unverified");
        return true;
    }
    /* Owner115: does a user-space HID device actually exist while reports flow? */
    static unsigned hidRegistryCounts(unsigned *bluetoothHid) {
        unsigned user=0,bt=0;
        OSDictionary *matching=IOService::serviceMatching("IOHIDUserDevice");
        if(matching) {
            OSIterator *it=IOService::getMatchingServices(matching);
            matching->release();
            if(it) {
                while(it->getNextObject()) { if(++user>=32)break; }
                it->release();
            }
        }
        matching=IOService::serviceMatching("IOHIDDevice");
        if(matching) {
            OSIterator *it=IOService::getMatchingServices(matching);
            matching->release();
            if(it) {
                OSObject *object;
                while((object=it->getNextObject())) {
                    IOService *service=OSDynamicCast(IOService,object);
                    if(!service)continue;
                    OSString *transport=OSDynamicCast(OSString,service->getProperty("Transport"));
                    if(transport && transport->isEqualTo("Bluetooth"))bt++;
                }
                it->release();
            }
        }
        if(bluetoothHid)*bluetoothHid=bt;
        return user;
    }
    unsigned hidReports=0;bool hidProbeFirst=false,hidProbeLater=false;
    int nativeStep() {
        if(nativeRxAt==nativeRxBytes) {
            Packet p;unsigned before=parser.bad,errors=nativeRxErrors;
            if(pump(p)) {
                if(pendingExtendedCreate && rewriteExtendedCreateResponse(p)) {
                    pendingExtendedCreate--;
                    IOLog("Pong Bluetooth105: extended create answer delivered as legacy0x200d opcode\n");
                }
                if(p.type==H4Acl && p.bytes>=9 && (p.data[6]|unsigned(p.data[7])<<8)==4 && p.data[8]==0x1b) {
                    hidReports++;
                    if((hidReports==1 && !hidProbeFirst) || (hidReports==8 && !hidProbeLater)) {
                        if(hidReports==1)hidProbeFirst=true; else hidProbeLater=true;
                        unsigned bt=0;unsigned user=hidRegistryCounts(&bt);
                        IOLog("Pong Bluetooth115: HID user devices=%u bluetoothHIDDevices=%u after %u report(s)\n",user,bt,hidReports);
                    }
                }
                if(legacyCreateTranslationEnabled() && translateEnhancedConnection(p))
                    IOLog("Pong Bluetooth113: enhanced connection complete delivered as legacy0x01 event\n");
                nativeRxBytes=nativeFrame(p,nativeRxFrame,sizeof(nativeRxFrame));nativeRxAt=0;
                if(!nativeRxBytes)return EPROTO;nativeRxPackets++;
                traceHci("RX",p);uint8_t status=0;
                if(nativeRxPackets<=8 && p.type==H4Event && p.bytes>=6 && p.data[0]==0x0e && commandComplete(p,uint16_t(p.data[3]|unsigned(p.data[4])<<8),status) && status)
                    IOLog("Pong Bluetooth79: native controller rejected opcode=%x status=%u; payload not logged\n",p.data[3]|unsigned(p.data[4])<<8,status);
            }
            if(parser.bad!=before || nativeRxErrors!=errors)return EPROTO;
        }
        if(nativeRxAt<nativeRxBytes)nativeRxAt+=qbt_transport_receive(nativeRxFrame+nativeRxAt,nativeRxBytes-nativeRxAt);
        if(nativeTxAt==nativeTxBytes){nativeTxBytes=qbt_transport_tx_take(nativeTxInput,sizeof(nativeTxInput));nativeTxAt=0;}
        for(unsigned i=0;i<sizeof(nativeTxInput) && nativeTxAt<nativeTxBytes;i++) {
            Packet p;unsigned bad=nativeTxParser.bad;
            bool complete=nativeTxParser.feed(nativeTxInput[nativeTxAt++],p);
            if(nativeTxParser.bad!=bad)return EPROTO;
            if(!complete)continue;
            if(!nativeTxAllowed(p)) {
                unsigned op=p.type==H4Command && p.bytes>=2?p.data[0]|unsigned(p.data[1])<<8:0;
                IOLog("Pong Bluetooth79: native TX refused type=%u opcode=%x; no unverified vendor firmware/NVM writes or forged reply\n",p.type,op);return EOPNOTSUPP;
            }
            traceHci("TX",p);
            unsigned txOpcode=p.type==H4Command && p.bytes>=2?(p.data[0]|unsigned(p.data[1])<<8):0;
            if(p.type==H4Command && txOpcode==0x2011 && p.bytes==10 && p.data[2]==7) {
                bool zero=true;
                for(unsigned i=0;i<6;i++){whiteListAddress[i]=p.data[4+i];if(p.data[4+i])zero=false;}
                whiteListType=p.data[3];whiteListValid=!zero;
                IOLog("Pong Bluetooth104: white list entry cached type=%x valid=%u; no address bytes\n",whiteListType,whiteListValid?1:0);
            }
            if(p.type==H4Command && txOpcode==0x200d && directPeerEnabled() &&
               directPeerFromWhiteList(p,whiteListAddress,whiteListType,whiteListValid))
                IOLog("Pong Bluetooth104: initiating command switched from white list to the cached direct peer address type=%x; no address bytes\n",whiteListType);
            if(p.type==H4Command && txOpcode==0x0c6d && p.bytes>=5 && p.data[2]>=2 && p.data[4]!=0) {
                p.data[4]=0;
                if(leHostFixTraces++<1)
                    IOLog("Pong Bluetooth105: Write_LE_Host_Support Simultaneous_LE_Host1->0 (Core4.1) forwarded\n");
            }
            if(p.type==H4Command && txOpcode==0x200d && legacyCreateTranslationEnabled() && translateLegacyCreate(p)) {
                if(pendingExtendedCreate<256)pendingExtendedCreate++;
                IOLog("Pong Bluetooth105: legacy0x200d translated to extended0x2043 one1M PHY; no address bytes\n");
            }
            if(normalizeLegacyInitiate(p,normalizeInitiateEnabled()))
                IOLog("Pong Bluetooth102: LE CreateConn normalized scan=%x/%x filter=%x peerType=%x ownType=%x interval=%x-%x latency=%x timeout=%x; original logged above\n",
                    hciRd16(p.data+3),hciRd16(p.data+5),p.data[7],p.data[8],p.data[15],
                    hciRd16(p.data+16),hciRd16(p.data+18),hciRd16(p.data+20),hciRd16(p.data+22));
            uint8_t frame[1029];unsigned n=nativeFrame(p,frame,sizeof(frame));
            if(!n || !send(frame,n))return EIO;nativeTxPackets++;break;
        }
        return 0;
    }
    void nativeSummary(const char *stage) {
        uint64_t io[4];pong_bluetooth_native_counters(io);
        IOLog("Pong Bluetooth79: native %s activity power/boot/control/channel=%u/%u/%u/%u active=%u txPackets=%u rxPackets=%u ringTX/RX=%llu/%llu boot=%llu IBSwake=%u IBSsleep=%u hciTrace=%u; not pairing/HID proof\n",stage,
            qbt_transport_activity[0],qbt_transport_activity[1],qbt_transport_activity[2],qbt_transport_activity[3],qbt_transport_active()?1:0,nativeTxPackets,nativeRxPackets,io[0],io[1],io[3],ibsWake,ibsSleep,nativeHciTraces);
    }
    void nativeServe() {
        if(!nativeOpen()){restore();qbt_transport_boot_result(ENODEV,nullptr,115200);return;}
        uint64_t began=now(),idleDeadline=began+120000,summaryAt=began+10000;unsigned summaries=0;bool wasActive=false,traffic=false,powered=true;
        for(;;) {
            uint32_t request=__atomic_exchange_n(&nativeRequest,0u,__ATOMIC_ACQ_REL);
            if(request==1) {
                int error=0;
                if(!powered){nativeMode=false;parser.reset();queueRead=queueWrite=0;powered=prepareController() && nativeOpen();if(!powered){restore();error=EIO;}}
                qbt_transport_power_result(true,error,nullptr,115200);idleDeadline=now()+120000;
            } else if(request==2) {
                if(powered){nativeMode=false;restore();powered=false;}
                pong_bluetooth_native_ready(false);qbt_transport_power_result(false,0,nullptr,115200);
            }
            if(powered) {
                bool active=qbt_transport_active();
                if(active)idleDeadline=now()+120000;
                if(active && !wasActive)nativeSummary("channel-open");
                if(!active && wasActive)idleDeadline=now()+500;
                wasActive=active;
                int error=nativeStep();
                if(error) {
                    __atomic_store_n(&nativeFatal,unsigned(error),__ATOMIC_RELEASE);nativeSummary("FAIL");
                    nativeMode=false;restore();powered=false;pong_bluetooth_native_ready(false);qbt_transport_power_result(false,error,nullptr,115200);
                    PongProbeRecord(7,"native UART.SK",Fail,"real bridge fault/unsupported native request; original daemon may need matching22H374 adapter",error);return;
                }
                uint64_t io[4];pong_bluetooth_native_counters(io);
                if(!traffic && active && nativeTxPackets && nativeRxPackets && io[0] && io[1]){traffic=true;PongProbeRecord(7,"native HCI traffic",Pass,"real commands/controller packets consumed by native rings; pairing/HID NOT verified",nativeTxPackets,nativeRxPackets);}
                if(now()>=idleDeadline){nativeSummary("idle-stop");nativeMode=false;restore();powered=false;pong_bluetooth_native_ready(false);qbt_transport_power_result(false,0,nullptr,115200);}
            }
            if(now()>=summaryAt && summaries<2){nativeSummary("snapshot");summaryAt=began+90000;summaries++;}
            __atomic_store_n(&nativePending,0u,__ATOMIC_RELEASE);IOSleep(powered?1:10);
        }
    }
#endif
    void run() {
#ifdef PONG_BLUETOOTH_NATIVE
        struct qbt_transport_ops ops={this,nativeWake,nativePower};int initialized=qbt_transport_init(&ops);
        if(initialized){PongProbeRecord(7,"native UART.SK",Fail,"pipe owner initialization failed",initialized);return;}
#endif
        if(!prepareController()) {
#ifdef PONG_BLUETOOTH_NATIVE
            qbt_transport_boot_result(EIO,nullptr,115200);
#endif
            return;
        }
#ifdef PONG_BLUETOOTH_NATIVE
        nativeServe();
#else
        discover();restore();
#endif
    }
};
static Transport transport;
static void worker(void *,wait_result_t){transport.run();}
}
extern "C" void pong_bluetooth_start_optional() {
    unsigned enabled=0;PE_parse_boot_argn("bt",&enabled,sizeof(enabled));if(enabled!=1)return;
    static uint32_t started;if(__atomic_exchange_n(&started,1,__ATOMIC_ACQ_REL))return;
    thread_t t=THREAD_NULL;if(kernel_thread_start(pongbt::worker,nullptr,&t)!=KERN_SUCCESS){IOLog("Pong Bluetooth75: worker SKIP unavailable\n");return;}thread_deallocate(t);
}
extern "C" void pong_bluetooth_panic_dump() {
    for(unsigned i=0;i<8;i++) {
        auto &p=pongbt::probes[i];unsigned s=__atomic_load_n(&p.sequence,__ATOMIC_ACQUIRE);if(!s)continue;
        if(s&1){printf("Pong Bluetooth75: snapshot%u RUNNING\n",i);continue;}
        const char *stage=p.stage,*reason=p.reason;unsigned result=p.result;uint64_t a=p.a,b=p.b;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);if(s!=__atomic_load_n(&p.sequence,__ATOMIC_ACQUIRE))continue;
        printf("Pong Bluetooth75: snapshot%u state=%u %s %s a=%llx b=%llx\n",i,result,stage?stage:"?",reason?reason:"?",a,b);
    }
}
