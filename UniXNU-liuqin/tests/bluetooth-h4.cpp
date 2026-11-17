/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../drivers/PongBluetooth/PongBluetoothProtocol.h"
#include <cassert>
#include <cstdio>
using namespace pongbt;
int main() {
    uint8_t buffer[260]={};assert(command(buffer,sizeof buffer,0xc03,nullptr,0)==4);
    assert(buffer[0]==1 && buffer[1]==3 && buffer[2]==12 && !buffer[3]);
    assert(!command(buffer,3,0xc03,nullptr,0));assert(!command(buffer,sizeof buffer,1,nullptr,1));
    Parser parser;Packet p;uint8_t status=99;
    const uint8_t reset[]={4,14,4,1,3,12,0};
    for(unsigned i=0;i<sizeof reset;i++)assert(parser.feed(reset[i],p)==(i==sizeof reset-1));
    assert(commandComplete(p,0xc03,status)&&!status);assert(!commandComplete(p,0x1001,status));
    p.data[5]=7;assert(commandComplete(p,0xc03,status)&&status==7);
    const uint8_t vendor[]={4,255,3,0xfc,0xfd,0xfe};
    for(unsigned i=0;i<sizeof vendor;i++)assert(parser.feed(vendor[i],p)==(i==sizeof vendor-1));
    assert(p.type==4 && p.bytes==5 && p.data[2]==IbsAck && p.data[4]==IbsSleep);
    assert(parser.feed(IbsWake,p)&&p.type==IbsWake&&!p.bytes);
    const uint8_t cs[]={4,15,4,0x0c,1,3,12};
    for(uint8_t b:cs)parser.feed(b,p);assert(commandStatus(p,0xc03,status)&&status==12);
    // ACL oversized payload is discarded through the declared length, including
    // bytes equal to H4/IBS markers; it cannot fabricate a command completion.
    const uint8_t acl[]={2,1,0,1,8};for(uint8_t b:acl)parser.feed(b,p);
    for(unsigned i=0;i<2049;i++)assert(!parser.feed(4,p));
    for(unsigned i=0;i<sizeof reset;i++)assert(parser.feed(reset[i],p)==(i==sizeof reset-1));
    assert(commandComplete(p,0xc03,status)&&!status && parser.bad==1);
    // A truncated oversized old session cannot swallow the next controller's
    // genuine Reset response. Preserve the old failure counter as evidence.
    for(uint8_t byte:acl)parser.feed(byte,p);assert(parser.discard);
    parser.reset();assert(parser.discard);parser.beginSession();assert(!parser.discard&&parser.bad==2);
    for(unsigned i=0;i<sizeof reset;i++)assert(parser.feed(reset[i],p)==(i==sizeof reset-1));
    assert(commandComplete(p,0xc03,status)&&!status&&parser.bad==2);
    assert(fifoBytes(0x90000002,16)==5);assert(fifoBytes(0x80000002,16)==8);
    assert(!fifoBytes(0xf0000002,16));assert(!fifoBytes(17,16));
    assert(sampling(0x20050000)==16 && sampling(0x20040000)==32);
    assert(7372800/(sampling(0x20050000)*4)==115200);
    assert(7372800/(sampling(0x20040000)*2)==115200);
    assert(packing(0)==0x4380e && packing(2)==0xc3e0e);
    puts("PASS Bluetooth H4: real length/status/opcode contracts, split frames, IBS payload isolation, oversized discard, FIFO tails and UART115200 packing/clock math; host only.");
}
