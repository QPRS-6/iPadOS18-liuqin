#!/usr/bin/env python3
"""Execute the actual bounded owner logger; no controller or device opened."""
import argparse,json,subprocess,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
TEMP=Path('/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode')

FIXTURE=r'''
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "PongBluetoothProtocol.h"
using namespace pongbt;
static std::vector<std::string> logs;
static bool PE_parse_boot_argn(const char *,void *,int) {return false;}
static void IOLog(const char *format,...) {
    char buffer[512];va_list args;va_start(args,format);
    int n=vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    if(n<0 || unsigned(n)>=sizeof(buffer)){puts("FAIL unbounded logger");std::abort();}
    logs.emplace_back(buffer);
}
struct Recording {
    unsigned nativeHciTraces=0,controllerSessions=2;
    unsigned attServices=0,attSummaries=0;bool attHid=false;unsigned attMtu=0;
    unsigned hidStart=0,hidEnd=0,hidReadTraces=0,hidWriteTraces=0;bool hidRange=false;
'''

CHECKS=r'''
};
#define CHECK(c) do{if(!(c)){printf("FAIL metadata check line=%u\n",__LINE__);return 1;}}while(0)
static bool contains(const char *text) {
    for(const auto &line:logs)if(line.find(text)!=std::string::npos)return true;
    return false;
}
int main() {
    unsigned cases=0;Recording t;Packet p;p.type=H4Command;p.bytes=10;
    const uint8_t scan[]={0x0b,0x20,7,1,0xe0,1,0x30,0,1,0};
    memcpy(p.data,scan,sizeof(scan));p.data[10]=0x0d;Packet before=p;
    t.traceHci("TX",p);
    CHECK(contains("type=1 interval=1e0 window=30 ownAddr=1 filter=0"));
    CHECK(!memcmp(&p,&before,sizeof(p)));cases++;
    for(unsigned length:{9u,11u}) {
        logs.clear();p.bytes=length;t.traceHci("TX",p);
        CHECK(!contains("LE SetScanParams"));cases++;
    }
    logs.clear();p.bytes=10;p.data[2]=6;t.traceHci("TX",p);
    CHECK(!contains("LE SetScanParams"));cases++;
    p=Packet{};p.type=H4Event;p.bytes=70;
    p.data[0]=0x0e;p.data[1]=68;p.data[2]=1;p.data[3]=2;p.data[4]=0x10;
    p.data[43]=0xff;p.data[42]=0x20;p.data[44]=0x40;
    logs.clear();before=p;t.traceHci("RX",p);
    CHECK(contains("supported-commands[37]=ff"));CHECK(!memcmp(&p,&before,sizeof(p)));cases++;
    for(unsigned length:{43u,44u,69u,71u}) {
        logs.clear();p.bytes=length;p.data[1]=uint8_t(length-2);t.traceHci("RX",p);
        CHECK(!contains("LE supported-commands"));cases++;
    }
    logs.clear();p.bytes=70;p.data[1]=68;p.data[5]=1;t.traceHci("RX",p);
    CHECK(contains("status=1"));CHECK(!contains("LE supported-commands"));cases++;
    logs.clear();p.data[5]=0;p.data[1]=67;t.traceHci("RX",p);
    CHECK(logs.empty());cases++;
    p=Packet{};p.type=H4Command;p.bytes=9;p.data[0]=5;p.data[1]=0x20;p.data[2]=6;
    memcpy(p.data+3,"SECRET",6);logs.clear();t.traceHci("TX",p);
    CHECK(logs.size()==1 && !contains("SECRET") && !contains("LE SetScanParams"));cases++;
    p=Packet{};p.type=H4Command;p.bytes=6;p.data[0]=0x24;p.data[1]=0x0c;p.data[2]=3;
    p.data[3]=0x0c;p.data[4]=1;p.data[6]=0xff;logs.clear();before=p;t.traceHci("TX",p);
    CHECK(contains("ClassOfDevice=00010c major=1 minor=3") && !memcmp(&p,&before,sizeof(p)));cases++;
    logs.clear();p.data[2]=2;t.traceHci("TX",p);CHECK(!contains("ClassOfDevice"));cases++;
    logs.clear();p.data[2]=3;p.bytes=5;t.traceHci("TX",p);CHECK(!contains("ClassOfDevice"));cases++;
    p=Packet{};p.type=H4Command;p.bytes=4;p.data[0]=0x1a;p.data[1]=0x0c;p.data[2]=1;p.data[3]=3;
    logs.clear();t.traceHci("TX",p);CHECK(contains("classic ScanEnable=3"));cases++;
    logs.clear();p.bytes=5;t.traceHci("TX",p);CHECK(!contains("classic ScanEnable"));cases++;
    // Owner102: the rejected legacy initiating command must expose only scalars.
    p=Packet{};p.type=H4Command;p.bytes=28;p.data[0]=0x0d;p.data[1]=0x20;p.data[2]=25;
    p.data[3]=0;p.data[4]=0;p.data[5]=0x30;p.data[6]=0;p.data[7]=0;p.data[8]=1;
    memcpy(p.data+9,"ADDR12",6);p.data[15]=1;p.data[16]=6;p.data[18]=0x0c;
    p.data[20]=0;p.data[22]=0xf4;p.data[24]=0;p.data[26]=0;
    logs.clear();before=p;t.traceHci("TX",p);
    CHECK(logs.size()==3 && contains("LE CreateConn scan=0/30 filter=0 peerType=1 ownType=1 interval=6-c latency=0 timeout=f4 ce=0-0") && contains("peerAddressZero=0"));
    CHECK(!contains("ADDR12") && !memcmp(&p,&before,sizeof(p)));cases++;
    p.bytes=27;logs.clear();t.traceHci("TX",p);CHECK(!contains("LE CreateConn"));cases++;
    p=Packet{};p.type=H4Command;p.bytes=53;p.data[0]=0x43;p.data[1]=0x20;p.data[2]=50;
    p.data[3]=0x60;p.data[5]=0x30;p.data[7]=0;p.data[8]=1;p.data[9]=1;
    memcpy(p.data+10,"ADDR34",6);p.data[16]=1;p.data[17]=6;p.data[19]=0x0c;
    logs.clear();before=p;t.traceHci("TX",p);
    CHECK(logs.size()==2 && contains("LE ExtCreateConn scan=60/30 filter=0 ownType=1 peerType=1 phys=1 interval1M=6-c"));
    CHECK(!contains("ADDR34") && !memcmp(&p,&before,sizeof(p)));cases++;
    p=Packet{};p.type=H4Event;p.bytes=14;p.data[0]=0x0e;p.data[1]=12;p.data[2]=1;
    p.data[3]=3;p.data[4]=0x20;p.data[5]=0;
    for(unsigned i=0;i<8;i++)p.data[6+i]=uint8_t(i+1);
    logs.clear();t.traceHci("RX",p);CHECK(logs.size()==2 && contains("LE features=0807060504030201"));cases++;
    p.data[5]=1;logs.clear();t.traceHci("RX",p);CHECK(logs.size()==1 && !contains("LE features"));cases++;
    p=Packet{};p.type=H4Event;p.bytes=7;p.data[0]=0x0e;p.data[1]=5;p.data[2]=1;p.data[3]=0x0f;p.data[4]=0x20;p.data[5]=0;p.data[6]=8;
    logs.clear();t.traceHci("RX",p);CHECK(logs.size()==2 && contains("LE white list size=8"));cases++;
    p=Packet{};p.type=H4Command;p.bytes=28;p.data[0]=0x0d;p.data[1]=0x20;p.data[2]=25;
    t.nativeHciTraces=512;logs.clear();t.traceHci("TX",p);
    CHECK(logs.size()==3 && contains("LE CreateConn") && contains("peerAddressZero=1"));cases++;
    p=Packet{};p.type=H4Command;p.bytes=3;p.data[0]=0x16;p.data[1]=0x20;p.data[2]=0;
    logs.clear();t.traceHci("TX",p);CHECK(logs.size()==1 && contains("opcode=2016"));cases++;
    p.data[0]=0x03;p.data[1]=0x0c;logs.clear();t.traceHci("TX",p);CHECK(logs.empty());cases++;
    p=Packet{};p.type=H4Command;p.bytes=9;p.data[0]=0x42;p.data[1]=0x20;p.data[2]=6;
    p.data[3]=1;p.data[4]=0;p.data[5]=0;p.data[6]=0;p.data[7]=0;p.data[8]=0;
    logs.clear();t.traceHci("TX",p);CHECK(logs.size()==2 && contains("LE ExtScanEnable enable=1 duplicates=0 duration=0 period=0"));cases++;
    p=Packet{};p.type=H4Command;p.bytes=11;p.data[0]=0x41;p.data[1]=0x20;p.data[2]=8;
    p.data[3]=1;p.data[4]=0;p.data[5]=1;p.data[6]=0;p.data[7]=0x60;p.data[8]=0x00;p.data[9]=0x30;p.data[10]=0x00;
    logs.clear();t.traceHci("TX",p);CHECK(logs.size()==2 && contains("LE ExtScanParams ownType=1 filter=0 phys=1 type0=0 interval0=60 window0=30"));cases++;
    p=Packet{};p.type=H4Event;p.bytes=4;p.data[0]=0x3e;p.data[1]=2;p.data[2]=1;p.data[3]=0;
    logs.clear();t.traceHci("RX",p);CHECK(logs.size()==1 && contains("LE meta subevent=1 status=0"));cases++;
    p.data[2]=2;logs.clear();t.traceHci("RX",p);CHECK(!contains("LE meta subevent"));cases++;
    p.type=H4Acl;logs.clear();unsigned budget=t.nativeHciTraces;t.traceHci("RX",p);
    CHECK(logs.empty() && t.nativeHciTraces==budget);cases++;
    p=Packet{};p.type=H4Command;p.bytes=3;p.data[0]=3;p.data[1]=0x0c;
    t.nativeHciTraces=511;logs.clear();t.traceHci("TX",p);CHECK(logs.size()==1);
    for(unsigned i=0;i<100;i++)t.traceHci("TX",p);
    CHECK(logs.size()==1 && t.nativeHciTraces==512);cases++;
    // Owner102: legacy initiating parameters are rewritten only when the Core
    // specification forbids them, and only when explicitly enabled.
    p=Packet{};p.type=H4Command;p.bytes=28;p.data[0]=0x0d;p.data[1]=0x20;p.data[2]=25;
    p.data[3]=0x60;p.data[5]=0x30;p.data[7]=0;p.data[8]=1;memcpy(p.data+9,"ADDR12",6);
    p.data[15]=1;p.data[16]=6;p.data[18]=0x0c;p.data[20]=0;p.data[22]=0xf4;
    before=p;CHECK(!t.normalizeLegacyInitiate(p,true) && !memcmp(&p,&before,sizeof(p)));cases++;
    CHECK(!t.normalizeLegacyInitiate(p,false) && !memcmp(&p,&before,sizeof(p)));cases++;
    p.data[3]=0;p.data[4]=0;p.data[5]=0;p.data[6]=0;p.data[8]=2;
    CHECK(t.normalizeLegacyInitiate(p,true));
    CHECK(t.hciRd16(p.data+3)==0x60 && t.hciRd16(p.data+5)==0x30 && p.data[8]==0);cases++;
    p.data[8]=3;CHECK(t.normalizeLegacyInitiate(p,true) && p.data[8]==1);cases++;
    p.data[16]=0;p.data[17]=0;p.data[18]=0;p.data[19]=0;p.data[20]=0;p.data[21]=0;p.data[22]=0;p.data[23]=0;
    CHECK(t.normalizeLegacyInitiate(p,true));
    CHECK(t.hciRd16(p.data+16)==6 && t.hciRd16(p.data+18)==0x0c && t.hciRd16(p.data+20)==0 && t.hciRd16(p.data+22)==0x1f4);cases++;
    CHECK(!memcmp(p.data+9,"ADDR12",6));cases++;
    // Owner104: a white-list initiating command with an empty peer address is
    // redirected to the cached direct peer entry.
    {const uint8_t entry[6]={1,2,3,4,5,6};
     p=Packet{};p.type=H4Command;p.bytes=28;p.data[0]=0x0d;p.data[1]=0x20;p.data[2]=25;
     p.data[3]=0x60;p.data[5]=0x30;p.data[7]=1;p.data[8]=0;p.data[15]=1;
     CHECK(t.directPeerFromWhiteList(p,entry,1,true));
     CHECK(p.data[7]==0 && p.data[8]==1 && !memcmp(p.data+9,entry,6));cases++;
     CHECK(!t.directPeerFromWhiteList(p,entry,1,true));cases++;
     p.data[7]=1;p.data[9]=9;CHECK(!t.directPeerFromWhiteList(p,entry,1,true));cases++;
     p.data[9]=0;CHECK(!t.directPeerFromWhiteList(p,entry,1,false));cases++;}
    // Owner105: legacy initiating command translated to extended0x2043 (1M PHY),
    // and the controller answer opcode rewritten back to0x200d.
    {p=Packet{};p.type=H4Command;p.bytes=28;p.data[0]=0x0d;p.data[1]=0x20;p.data[2]=25;
     const uint8_t l[25]={0x30,0,0x30,0,1,0,'A','D','D','R','1','2',1,8,0,0x18,0,0,0,0x48,0,2,0,0x0c,0};
     memcpy(p.data+3,l,25);
     CHECK(t.translateLegacyCreate(p));
     CHECK(p.bytes==29 && p.data[0]==0x43 && p.data[1]==0x20 && p.data[2]==26);
     CHECK(p.data[3]==1 && p.data[4]==1 && p.data[5]==0 && p.data[12]==1);
     CHECK(p.data[6]=='A' && p.data[11]=='2' && p.data[13]==0x30 && p.data[15]==0x30);
     CHECK(p.data[17]==8 && p.data[19]==0x18 && p.data[23]==0x48 && p.data[27]==0x0c);cases++;
     p=Packet{};p.type=H4Event;p.bytes=7;p.data[0]=0x0f;p.data[1]=4;p.data[2]=0x0c;p.data[3]=1;p.data[4]=0x43;p.data[5]=0x20;
     CHECK(t.rewriteExtendedCreateResponse(p) && p.data[4]==0x0d && p.data[5]==0x20);cases++;
     p.data[4]=0x42;CHECK(!t.rewriteExtendedCreateResponse(p));cases++;}
    // Owner106: classic security outcomes and L2CAP channels, no address/key data.
    p=Packet{};p.type=H4Event;p.bytes=13;p.data[0]=0x03;p.data[1]=11;p.data[2]=0;p.data[11]=1;p.data[12]=1;
    memcpy(p.data+3,"ADDR56",6);
    logs.clear();t.traceHci("RX",p);
    CHECK(logs.size()==1 && contains("classic ConnectionComplete status=0 linkType=1 encryption=1") && !contains("ADDR56"));cases++;
    p=Packet{};p.type=H4Event;p.bytes=6;p.data[0]=0x05;p.data[1]=4;p.data[2]=0;p.data[5]=0x13;
    logs.clear();t.traceHci("RX",p);CHECK(logs.size()==1 && contains("classic DisconnectComplete status=0 reason=13"));cases++;
    p=Packet{};p.type=H4Acl;p.bytes=9;p.data[2]=5;p.data[3]=0;p.data[6]=6;p.data[7]=0;
    logs.clear();t.traceHci("TX",p);CHECK(logs.size()==2 && contains("ACL TX cid=6 payload=5"));cases++;
    p=Packet{};p.type=H4Event;p.bytes=7;p.data[0]=0x07;p.data[1]=5;p.data[2]=4;
    logs.clear();t.traceHci("RX",p);CHECK(logs.size()==1 && contains("RemoteNameRequestComplete status=4"));cases++;
    // Owner107: ATT/SMP opcodes and the HID-over-GATT UUID presence, no values.
    p=Packet{};p.type=H4Acl;p.bytes=9;p.data[2]=5;p.data[3]=0;p.data[6]=6;p.data[7]=0;p.data[8]=0x21;
    logs.clear();t.traceHci("TX",p);CHECK(logs.size()==2 && contains("SMP op=21"));cases++;
    p=Packet{};p.type=H4Acl;p.bytes=13;p.data[2]=9;p.data[3]=0;p.data[6]=4;p.data[7]=0;
    p.data[8]=0x01;p.data[9]=0x0a;p.data[10]=9;p.data[11]=0;p.data[12]=0x05;
    logs.clear();t.traceHci("RX",p);CHECK(logs.size()==2 && contains("ATT error request=a handle=9 error=5"));cases++;
    p=Packet{};p.type=H4Event;p.bytes=12;p.data[0]=0x3e;p.data[1]=10;p.data[2]=0x02;p.data[3]=1;
    p.data[6]=0x02;p.data[7]=0x12;p.data[8]=0x18;
    logs.clear();t.traceHci("RX",p);CHECK(logs.size()==1 && contains("advertisesHID=1"));cases++;
    p=Packet{};p.type=H4Acl;p.bytes=16;p.data[2]=12;p.data[3]=0;p.data[6]=4;p.data[7]=0;
    p.data[8]=0x11;p.data[9]=6;p.data[10]=1;p.data[11]=0;p.data[12]=5;p.data[13]=0;
    p.data[14]=0x12;p.data[15]=0x18;
    logs.clear();t.traceHci("RX",p);CHECK(contains("GATT services=1 hid=1"));cases++;
    p=Packet{};p.type=H4Acl;p.bytes=11;p.data[2]=7;p.data[3]=0;p.data[6]=4;p.data[7]=0;
    p.data[8]=0x0a;p.data[9]=2;p.data[10]=0;
    logs.clear();t.traceHci("TX",p);CHECK(contains("HID read request handle=2"));cases++;
    // Owner113: enhanced connection complete delivered in the legacy layout.
    p=Packet{};p.type=H4Event;p.bytes=33;p.data[0]=0x3e;p.data[1]=31;p.data[2]=0x0a;p.data[3]=0;
    p.data[4]=1;p.data[5]=0;p.data[6]=0;p.data[7]=1;memcpy(p.data+8,"ADDR78",6);
    p.data[26]=0x18;p.data[28]=0;p.data[30]=0xc8;p.data[32]=0;
    CHECK(t.translateEnhancedConnection(p));
    CHECK(p.bytes==21 && p.data[1]==19 && p.data[2]==0x01);
    CHECK(p.data[3]==0 && p.data[8]=='A' && p.data[14]==0x18 && p.data[16]==0 && p.data[18]==0xc8 && p.data[20]==0);cases++;
    p=Packet{};p.type=H4Event;p.bytes=21;p.data[0]=0x3e;p.data[1]=19;p.data[2]=0x01;
    CHECK(!t.translateEnhancedConnection(p));cases++;
    p=Packet{};p.type=H4Acl;p.bytes=12;p.data[2]=8;p.data[3]=0;p.data[4]=6;p.data[5]=0;
    p.data[6]=4;p.data[7]=0;p.data[8]=0x1b;p.data[9]=0x12;p.data[10]=0;p.data[11]=1;
    logs.clear();t.traceHci("RX",p);CHECK(contains("notification handle=12 length=3"));cases++;
    CHECK(cases==57);puts("PASS57 actual HCI logger: exact H4-excluded shapes/status, stale-tail refusal, bounded non-sensitive classification/scan-control/initiating scalars, budget-independent connect evidence and no address/name/key payload emission; host only.");
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=ROOT/'drivers/PongBluetooth/PongBluetooth.cpp')
    p.add_argument('--expect-offset-failure',action='store_true')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    source=a.source.read_text();start=source.index('    static bool directPeerEnabled() {');end=source.index('\n#endif',start)
    body=source[start:end];a.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='bt88-metadata-',dir=TEMP) as td:
        file=Path(td)/'logger.cpp';exe=Path(td)/'logger';file.write_text(FIXTURE+body+CHECKS)
        command=['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                 '-I'+str(ROOT/'drivers/PongBluetooth'),str(file),'-o',str(exe)]
        c=subprocess.run(command,text=True,capture_output=True);(a.output/'compile.log').write_text(c.stdout+c.stderr)
        if c.returncode:print(c.stdout+c.stderr)
        c.check_returncode();v=subprocess.run([str(exe)],text=True,capture_output=True,timeout=20)
        (a.output/'test.log').write_text(v.stdout+v.stderr)
        if a.expect_offset_failure:
            expected=v.returncode==1 and 'FAIL metadata check' in v.stdout
            assert expected,'Baseline did not reproduce the offset failure'
            print('PASS expected-negative87: actual old logger shifts scan params and reads a stale tail; host only.')
        else:
            v.check_returncode();print(v.stdout.strip());expected=False
        (a.output/'report.json').write_text(json.dumps(dict(result='PASS',source=str(a.source),
            command=command,cases=19 if not expected else 1,expected_failure=expected,
            hardware_verified=False,scope='Actual owner traceHci body with recording IOLog and ASan/UBSan; no device execution'),indent=2)+'\n')

if __name__=='__main__':main()
