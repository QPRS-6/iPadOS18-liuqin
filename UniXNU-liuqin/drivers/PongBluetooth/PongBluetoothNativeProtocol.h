/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_BLUETOOTH_NATIVE_PROTOCOL_H
#define PONG_BLUETOOTH_NATIVE_PROTOCOL_H
#include "PongBluetoothProtocol.h"
#include <string.h>
namespace pongbt {
inline bool nativeClientAllowed(const char *name,bool platform,bool entitled) {
    return name && platform && entitled && (!strcmp(name,"bluetoothd") || !strcmp(name,"BlueTool"));
}
inline bool nativeAddress(const uint8_t wire[6],uint8_t network[6]) {
    unsigned zero=0,ones=0;
    for(unsigned i=0;i<6;i++){zero+=wire[i]==0;ones+=wire[i]==255;}
    if(zero==6 || ones==6)return false;
    // Actual79 controller reply is the non-unique QCA placeholder
    // 00:00:00:00:5a:ad. Do not expose it as a qualified native identity.
    if(wire[0]==0xad && wire[1]==0x5a && wire[2]==0 && wire[3]==0 && wire[4]==0 && wire[5]==0)return false;
    for(unsigned i=0;i<6;i++)network[i]=wire[5-i];
    return true;
}
inline bool nativeTxAllowed(const Packet &p) {
    if(p.bytes>sizeof(p.data))return false;
    if(p.type==H4Acl || p.type==H4Iso) {
        if(p.bytes<4)return false;
        unsigned payload=unsigned(p.data[2])|unsigned(p.data[3])<<8;
        if(p.type==H4Iso)payload&=0x3fff;
        return p.bytes==payload+4;
    }
    if(p.type==H4Sco)return p.bytes>=3 && p.bytes==unsigned(p.data[2])+3;
    if(p.type!=H4Command || p.bytes<3 || p.bytes!=unsigned(p.data[2])+3)return false;
    unsigned opcode=unsigned(p.data[0])|unsigned(p.data[1])<<8,group=opcode>>10;
    if(group && group<=8)return true;
    // Only the documented read-only QCA version/build queries. Do not forward
    // another manufacturer's firmware/NVM writes to an unidentified QCA SoC.
    return opcode==0xfc00 && p.bytes==4 && (p.data[3]==0x19 || p.data[3]==0x20);
}
inline unsigned nativeFrame(const Packet &p,uint8_t *out,unsigned capacity) {
    if(p.type<H4Command || p.type>H4Iso || p.bytes>sizeof(p.data) || capacity<p.bytes+1)return 0;
    out[0]=p.type;for(unsigned i=0;i<p.bytes;i++)out[i+1]=p.data[i];return p.bytes+1;
}
}
#endif
