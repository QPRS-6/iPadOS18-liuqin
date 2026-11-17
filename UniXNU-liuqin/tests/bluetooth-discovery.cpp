/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../drivers/PongBluetooth/PongBluetoothDiscovery.h"
#include <cassert>
#include <cstdio>
using namespace pongbt;
int main(){
 Packet p;p.type=H4Event;p.bytes=20;p.data[0]=14;p.data[1]=18;p.data[3]=0;p.data[4]=0xfc;p.data[6]=0x19;
 p.data[7]=12;p.data[8]=2;p.data[12]=3;p.data[14]=0x21;p.data[16]=4;Version v;assert(qcaVersion(p,v)&&v.product==2&&v.patch==3&&v.rom==0x21&&v.soc==4);
 p.data[7]=1;assert(!qcaVersion(p,v));p.data[7]=0;assert(!qcaVersion(p,v));p.data[7]=12;p.bytes=19;assert(!qcaVersion(p,v));
 p.bytes=20;p.data[3]=1;assert(!qcaVersion(p,v));p.data[3]=0;
 p.data[5]=1;assert(!qcaVersion(p,v));p.data[5]=0;
 p.data[14]=0;assert(!qcaVersion(p,v));p.data[14]=0x21;
 p.data[16]=0;assert(!qcaVersion(p,v));p.data[16]=4;
 Packet legacy;legacy.type=H4Event;legacy.bytes=16;legacy.data[0]=0xff;legacy.data[1]=14;legacy.data[3]=2;
 for(unsigned i=0;i<12;i++)legacy.data[4+i]=p.data[8+i];
 assert(qcaVersion(legacy,v)&&v.product==2&&v.patch==3&&v.rom==0x21&&v.soc==4);
 legacy.data[3]=3;assert(!qcaVersion(legacy,v));legacy.data[3]=2;
 legacy.data[2]=1;assert(!qcaVersion(legacy,v));legacy.data[2]=0;
 legacy.data[1]=13;assert(!qcaVersion(legacy,v));legacy.data[1]=14;
 Discovery counts;Packet adv;adv.type=H4Event;adv.bytes=18;adv.data[0]=0x3e;adv.data[1]=16;adv.data[2]=2;adv.data[3]=1;
 adv.data[12]=4;adv.data[13]=3;adv.data[14]=3;adv.data[15]=0x12;adv.data[16]=0x18;adv.data[17]=0x90;
 assert(discoveryEvent(adv,counts)&&counts.le==1&&counts.leHid==1);
 adv.data[12]=20;assert(!discoveryEvent(adv,counts)&&counts.le==1);
 adv.data[12]=4;adv.data[13]=5;assert(!discoveryEvent(adv,counts)&&counts.le==1);
 adv.data[13]=3;
 Packet multi=adv;multi.bytes=28;multi.data[1]=26;multi.data[3]=2;multi.data[26]=0;multi.data[27]=0x90;
 assert(discoveryEvent(multi,counts)&&counts.le==3&&counts.leHid==2);
 multi.bytes=27;multi.data[1]=25;assert(!discoveryEvent(multi,counts)&&counts.le==3&&counts.leHid==2);
 Packet inquiry;inquiry.type=H4Event;inquiry.bytes=31;inquiry.data[0]=0x22;inquiry.data[1]=29;inquiry.data[2]=2;
 inquiry.data[11]=0x40;inquiry.data[12]=5; // first peripheral keyboard
 inquiry.data[25]=0;inquiry.data[26]=4; // second audio device, not keyboard
 assert(discoveryEvent(inquiry,counts)&&counts.classic==2&&counts.classicKeyboard==1);
 inquiry.bytes=30;assert(!discoveryEvent(inquiry,counts)&&counts.classic==2);
 Packet extended;extended.type=H4Event;extended.bytes=257;extended.data[0]=0x2f;extended.data[1]=255;extended.data[2]=1;extended.data[11]=0x40;extended.data[12]=5;
 assert(discoveryEvent(extended,counts)&&counts.classic==3&&counts.classicKeyboard==2);
 extended.data[2]=2;assert(!discoveryEvent(extended,counts)&&counts.classic==3);
 puts("PASS Bluetooth discovery: modern/legacy QCAEDL length/status/ROM/SoC, transactional multi-LE TLV/HIDservice and classical RSSI/extended keyboardclass reports; no identity strings/addresses or device execution.");
}
