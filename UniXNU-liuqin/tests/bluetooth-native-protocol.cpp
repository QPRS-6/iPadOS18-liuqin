/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../drivers/PongBluetooth/PongBluetoothNativeProtocol.h"
#include <cassert>
#include <cstdio>
using namespace pongbt;
int main(){
 assert(nativeClientAllowed("bluetoothd",true,true));
 assert(nativeClientAllowed("BlueTool",true,true));
 assert(!nativeClientAllowed("Preferences",true,true));
 assert(!nativeClientAllowed("bluetoothd",false,true));
 assert(!nativeClientAllowed("bluetoothd",true,false));
 assert(!nativeClientAllowed(nullptr,true,true));
 uint8_t wire[]={1,2,3,4,5,7},network[6]={};assert(nativeAddress(wire,network));
 assert(network[0]==7&&network[5]==1); // Bluetooth address, not an Ethernet I/G filter
 uint8_t zero[6]={},ones[]={255,255,255,255,255,255};assert(!nativeAddress(zero,network)&&!nativeAddress(ones,network));
 uint8_t placeholder[]={0xad,0x5a,0,0,0,0};assert(!nativeAddress(placeholder,network));
 Packet p;p.type=H4Command;p.bytes=3;p.data[0]=3;p.data[1]=12;assert(nativeTxAllowed(p));
 p.data[0]=0;p.data[1]=0xfc;p.bytes=4;p.data[2]=1;p.data[3]=0x19;assert(nativeTxAllowed(p));
 p.data[3]=0x20;assert(nativeTxAllowed(p));
 p.data[3]=0x1e;assert(!nativeTxAllowed(p)); // QCA firmware download
 p.data[0]=0x0b;assert(!nativeTxAllowed(p)); // NVM write
 p.data[0]=0x14;assert(!nativeTxAllowed(p)); // address write
 p.data[0]=0x4c;assert(!nativeTxAllowed(p)); // another vendor's RAM/firmware operation
 p.type=H4Event;assert(!nativeTxAllowed(p)); // host cannot forge controller events
 p.type=IbsWake;p.bytes=0;assert(!nativeTxAllowed(p));
 p=Packet{};p.type=H4Acl;p.bytes=7;p.data[2]=3;p.data[4]=0xfc;p.data[5]=0xfd;p.data[6]=0xfe;assert(nativeTxAllowed(p));
 uint8_t frame[1029];unsigned n=nativeFrame(p,frame,sizeof(frame));assert(n==8&&frame[0]==H4Acl);
 Parser parser;Packet parsed;bool complete=false;for(unsigned i=0;i<n;i++)complete=parser.feed(frame[i],parsed);
 assert(complete&&parsed.type==H4Acl&&parsed.bytes==7&&parsed.data[4]==0xfc&&parsed.data[6]==0xfe);
 assert(!nativeFrame(p,frame,7));p.bytes=6;assert(!nativeTxAllowed(p));
 p=Packet{};p.type=H4Sco;p.bytes=4;p.data[2]=1;assert(nativeTxAllowed(p));
 p=Packet{};p.type=H4Iso;p.bytes=5;p.data[2]=1;p.data[3]=0xc0;assert(nativeTxAllowed(p));
 p.bytes=1029;assert(!nativeTxAllowed(p)&&!nativeFrame(p,frame,sizeof(frame)));
 puts("PASS native Bluetooth protocol: signed/entitled caller policy, real address order, H4 ACL/SCO/ISO lengths and IBS payload isolation; unqualified vendor firmware/NVM and forged event/control traffic refused. Host only.");
}
