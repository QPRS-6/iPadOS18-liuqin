/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_BLUETOOTH_PROTOCOL_H
#define PONG_BLUETOOTH_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
namespace pongbt {
enum {H4Command=1,H4Acl=2,H4Sco=3,H4Event=4,H4Iso=5,IbsAck=0xfc,IbsWake=0xfd,IbsSleep=0xfe};
struct Packet {uint8_t type=0,data[1028]={};unsigned bytes=0;};
// Length-delimited H4 framing: packet bytes are never treated as IBS tokens.
struct Parser {
    Packet packet;unsigned header=0,needed=0,discard=0;uint32_t bad=0;
    void reset(){packet.type=0;packet.bytes=0;header=needed=0;}
    // Only after an actual controller reset boundary. Normal frame reset must
    // retain oversized-payload discard; a new physical session must not.
    void beginSession(){reset();discard=0;}
    bool feed(uint8_t byte,Packet &out) {
        if(discard){if(!--discard)reset();return false;}
        if(!packet.type) {
            if(byte==IbsAck || byte==IbsWake || byte==IbsSleep){out.type=byte;out.bytes=0;return true;}
            if(byte<H4Command || byte>H4Iso){bad++;return false;}
            packet.type=byte;packet.bytes=0;header=byte==H4Event?2:(byte==H4Acl || byte==H4Iso?4:3);needed=header;return false;
        }
        packet.data[packet.bytes++]=byte;
        if(packet.bytes==header) {
            unsigned payload=packet.type==H4Event?packet.data[1]:
                (packet.type==H4Acl || packet.type==H4Iso?unsigned(packet.data[2])|unsigned(packet.data[3])<<8:packet.data[2]);
            if(packet.type==H4Iso)payload&=0x3fff;
            if(payload>sizeof(packet.data)-header){bad++;discard=payload;reset();return false;}
            needed=header+payload;
        }
        if(packet.bytes!=needed)return false;
        out=packet;reset();return true;
    }
};
inline bool commandComplete(const Packet &p,uint16_t opcode,uint8_t &status) {
    if(p.type!=H4Event || p.bytes<6 || p.data[0]!=0x0e || p.data[1]+2u!=p.bytes)return false;
    if((unsigned(p.data[3])|unsigned(p.data[4])<<8)!=opcode)return false;
    status=p.data[5];return true;
}
inline bool commandStatus(const Packet &p,uint16_t opcode,uint8_t &status) {
    if(p.type!=H4Event || p.bytes!=6 || p.data[0]!=0x0f || p.data[1]!=4)return false;
    if((unsigned(p.data[4])|unsigned(p.data[5])<<8)!=opcode)return false;
    status=p.data[2];return true;
}
inline size_t command(uint8_t *out,size_t capacity,uint16_t opcode,const uint8_t *data,unsigned bytes) {
    if(bytes>255 || capacity<bytes+4 || (bytes && !data))return 0;
    out[0]=H4Command;out[1]=uint8_t(opcode);out[2]=uint8_t(opcode>>8);out[3]=uint8_t(bytes);
    for(unsigned i=0;i<bytes;i++)out[4+i]=data[i];return bytes+4;
}
inline unsigned fifoBytes(uint32_t status,unsigned depth) {
    unsigned words=status&0x1ffffff;if(words>depth)return 0;
    unsigned tail=(status>>28)&7;
    if((status&0x80000000u) && words && tail){if(tail>4)return 0;return (words-1)*4+tail;}
    return words*4;
}
inline unsigned sampling(uint32_t version){return version>=0x20050000?16:32;}
inline uint32_t packing(unsigned first) {
    unsigned a=first*8*32+14,b=(first+1)*8*32+14;
    if(first==2)b|=1;return a|(b<<10);
}
}
#endif
