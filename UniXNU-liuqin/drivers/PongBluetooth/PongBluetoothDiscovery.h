/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_BLUETOOTH_DISCOVERY_H
#define PONG_BLUETOOTH_DISCOVERY_H
#include "PongBluetoothProtocol.h"
namespace pongbt {
inline uint16_t le16(const uint8_t *p){return uint16_t(unsigned(p[0])|unsigned(p[1])<<8);}
inline uint32_t le32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
struct Version {uint32_t product=0,soc=0;uint16_t patch=0,rom=0;};
inline bool qcaVersion(const Packet &p,Version &out) {
    const uint8_t *v=nullptr;
    if(p.type!=H4Event || p.bytes<2 || p.bytes!=p.data[1]+2u)return false;
    // Actual79: EDL header0/19/0c. The extra modern byte is the12-byte
    // version payload length, not another status byte (status is cresp0).
    if(p.data[0]==0x0e && p.bytes==20 && le16(p.data+3)==0xfc00 && p.data[5]==0 && p.data[6]==0x19 && p.data[7]==12)v=p.data+8;
    if(p.data[0]==0xff && p.bytes==16 && p.data[2]==0 && p.data[3]==2)v=p.data+4;
    if(!v)return false;
    Version parsed{le32(v),le32(v+8),le16(v+4),le16(v+6)};
    if(!parsed.soc || !parsed.rom)return false;out=parsed;return true;
}
struct Discovery {unsigned classic=0,classicKeyboard=0,le=0,leHid=0,malformed=0;};
inline bool discoveryEvent(const Packet &p,Discovery &counts) {
    if(p.type!=H4Event || p.bytes<2 || p.bytes!=p.data[1]+2u)return false;
    if(p.data[0]==0x22) {
        if(p.bytes<3){counts.malformed++;return false;}
        unsigned n=p.data[2];if(p.bytes!=3+14*n){counts.malformed++;return false;}
        counts.classic+=n;
        for(unsigned i=0;i<n;i++) {
            const uint8_t *cod=p.data+3+14*i+8;unsigned value=unsigned(cod[0])|unsigned(cod[1])<<8|unsigned(cod[2])<<16;
            if(((value>>8)&31)==5 && (value&0x40))counts.classicKeyboard++;
        }
        return true;
    }
    if(p.data[0]==0x2f) {
        if(p.bytes!=257 || p.data[2]!=1){counts.malformed++;return false;}
        counts.classic++;const uint8_t *cod=p.data+11;unsigned value=unsigned(cod[0])|unsigned(cod[1])<<8|unsigned(cod[2])<<16;
        if(((value>>8)&31)==5 && (value&0x40))counts.classicKeyboard++;
        return true;
    }
    if(p.data[0]!=0x3e || p.bytes<4 || p.data[2]!=2)return false;
    unsigned at=4,n=p.data[3],reports=0,hid=0;
    for(unsigned i=0;i<n;i++) {
        if(at+10>p.bytes){counts.malformed++;return false;}
        unsigned length=p.data[at+8];if(at+10+length>p.bytes){counts.malformed++;return false;}
        bool hasHid=false;unsigned a=at+9,end=a+length;
        while(a<end) {
            unsigned bytes=p.data[a++];if(!bytes)break;
            if(bytes>end-a){counts.malformed++;return false;}
            unsigned type=p.data[a++];
            if(type==2 || type==3)for(unsigned k=0;k+1<bytes-1;k+=2)if(le16(p.data+a+k)==0x1812)hasHid=true;
            a+=bytes-1;
        }
        reports++;hid+=hasHid;at+=10+length;
    }
    if(at!=p.bytes){counts.malformed++;return false;}
    counts.le+=reports;counts.leHid+=hid;return true;
}
}
#endif
