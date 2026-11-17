#!/usr/bin/env python3
"""Compile/run actual81 C paths with recording VFS/sysctl/HCI; no device I/O."""
import argparse,importlib.util,json,subprocess,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
TEMP=Path('/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode')

def run(source,out,extra=(),cpp=False):
    out.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='bt81-host-',dir=TEMP) as directory:
        directory=Path(directory);exe=directory/'test'
        (directory/'PongBluetoothRuntimeData.h').write_text('extern const unsigned char pong_bt_bluetool[],pong_bt_daemon[];\nextern const unsigned pong_bt_bluetool_size,pong_bt_daemon_size;\n')
        if isinstance(source,str):
            path=directory/('test.cpp' if cpp else 'test.c');path.write_text(source)
        else:path=source
        command=['xcrun','clang++' if cpp else 'clang','-std=c++17' if cpp else '-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-I'+str(ROOT/'tests'),'-I'+str(ROOT/'drivers/PongBluetooth'),'-I'+str(directory),*extra,str(path),'-o',str(exe)]
        compile_result=subprocess.run(command,text=True,capture_output=True);(out/'compile.log').write_text(compile_result.stdout+compile_result.stderr)
        if compile_result.returncode:print(compile_result.stdout+compile_result.stderr)
        compile_result.check_returncode()
        result=subprocess.run([str(exe)],text=True,capture_output=True,timeout=20);(out/'test.log').write_text(result.stdout+result.stderr)
        if result.returncode:print(result.stdout+result.stderr)
        result.check_returncode();print(result.stdout.splitlines()[-1])
        (out/'report.json').write_text(json.dumps(dict(result='PASS',command=command,hardware_verified=False,scope='Actual C source with host recording boundaries; no installed runtime, controller, pairing or HID proof'),indent=2)+'\n')

ADDRESS_PREFIX=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cerrno>
#include "PongBluetoothProtocol.h"
#include "PongBluetoothDiscovery.h"
#include "PongBluetoothAddress.h"
using namespace pongbt;
static uint64_t time_ms;
static uint64_t now(){return time_ms;}
static void IOSleep(unsigned ms){time_ms+=ms;}
#define IOLog(...) ((void)0)
enum {Fail=2};
static void PongProbeRecord(unsigned,const char*,unsigned,const char*){}
struct Recording {
    unsigned manufacturer=0x1d,received=0,events=0,scenario=0,writes=0,reads=0;
    uint8_t addressWire[6]={};Parser parser;
    struct {unsigned r(unsigned){return 0;}}se;
    std::vector<Packet> replies;std::vector<uint8_t> actual_write;
    void complete(unsigned opcode,unsigned status,unsigned address_bytes=6) {
        Packet p;p.type=H4Event;p.bytes=opcode==0x1009?6+address_bytes:6;
        p.data[0]=0x0e;p.data[1]=uint8_t(p.bytes-2);p.data[2]=1;p.data[3]=uint8_t(opcode);p.data[4]=uint8_t(opcode>>8);p.data[5]=uint8_t(status);
        if(opcode==0x1009)for(unsigned i=0;i<address_bytes;i++)p.data[6+i]=scenario==8?pong_bt_owner_address[i]:pong_bt_owner_address[5-i];
        if(opcode==0x1009&&scenario==7)p.data[6]^=1;
        replies.push_back(p);
    }
    bool send(const uint8_t *bytes,unsigned n) {
        assert(n>=4&&bytes[0]==H4Command&&bytes[3]+4u==n);unsigned opcode=unsigned(bytes[1])|unsigned(bytes[2])<<8;
        if(opcode==0xfc14) {
            writes++;actual_write.assign(bytes,bytes+n);if(scenario==3)return false;
            if(scenario==2||scenario==7){Packet p;p.type=H4Event;p.bytes=3;p.data[0]=0xff;p.data[1]=1;p.data[2]=0;replies.push_back(p);}
            else if(scenario==5){Packet p;p.type=H4Event;p.bytes=6;p.data[0]=0x0f;p.data[1]=4;p.data[2]=0x0c;p.data[3]=1;p.data[4]=0x14;p.data[5]=0xfc;replies.push_back(p);}
            else if(scenario!=6)complete(opcode,scenario==4?0x0c:0);
        } else {
            assert(opcode==0x1009);reads++;
            if(scenario==11)complete(0x1001,0);
            else complete(opcode,scenario==9?0x0c:0,scenario==10?5:6);
        }
        return true;
    }
    bool pump(Packet &packet){if(replies.empty())return false;packet=replies.front();replies.erase(replies.begin());received+=packet.bytes;events++;return true;}
'''
ADDRESS_CHECKS=r'''
};
int main(){unsigned cases=0;Version version;version.product=0x13;version.soc=0x400c0210;version.rom=0x201;version.patch=0x38e6;
 for(unsigned scenario=1;scenario<=11;scenario++){
  Recording t;t.scenario=scenario;time_ms=0;bool result=t.configureOwnerAddress(version);assert(result==(scenario==1||scenario==2));
  if(result)for(unsigned i=0;i<6;i++)assert(t.addressWire[i]==pong_bt_owner_address[5-i]);
  else for(unsigned i=0;i<6;i++)assert(!t.addressWire[i]);
  assert(t.writes==1&&t.actual_write.size()==10&&t.actual_write[1]==0x14&&t.actual_write[2]==0xfc&&t.actual_write[3]==6);
  assert(!memcmp(t.actual_write.data()+4,pong_bt_owner_address,6));
  if(scenario==3||scenario==4||scenario==5||scenario==6)assert(t.reads==0);else assert(t.reads==1);
  assert(time_ms<=3000);cases++;
 }
 for(unsigned field=0;field<4;field++){
  Version other=version;Recording t;if(field==0)other.product^=1;if(field==1)other.soc^=1;if(field==2)other.rom^=1;if(field==3)t.manufacturer^=1;
  assert(!t.configureOwnerAddress(other)&&!t.writes&&!t.reads);cases++;
 }
 Recording same;for(unsigned i=0;i<6;i++)same.addressWire[i]=pong_bt_owner_address[5-i];assert(same.configureOwnerAddress(version)&&!same.writes&&!same.reads);cases++;
 assert(cases==16);puts("PASS81 owner address: 16 actual write/transact recording cases; private owner input used without printing, network-order0xfc14 and actual0x1009 wire-order match required, identity/status/timeouts/short/different readbacks rejected. Host only.");
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--section',choices=('all','runtime','bootstrap','address'),default='all');p.add_argument('--output',type=Path,required=True)
    p.add_argument('--runtime',type=Path);p.add_argument('--address-input',type=Path);a=p.parse_args()
    if a.section in ('all','runtime'):run(ROOT/'tests/bluetooth-runtime.c',a.output/'runtime-tests')
    if a.section in ('all','bootstrap'):run(ROOT/'tests/bluetooth-bootstrap18.c',a.output/'bootstrap-tests',('-fblocks',))
    if a.section in ('all','address'):
        if not a.runtime or not a.address_input:p.error('--runtime and --address-input required for address cases')
        spec=importlib.util.spec_from_file_location('bt81_private_runtime',ROOT/'scripts/pong_bluetooth_userspace.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix='bt81-private-',dir=TEMP) as directory:
            directory=Path(directory);module.prepare_runtime(a.runtime,a.address_input,directory)
            source=(ROOT/'drivers/PongBluetooth/PongBluetooth.cpp').read_text()
            transact=source[source.index('    bool transact('):source.index('    void discover()')]
            owner=source[source.index('    bool configureOwnerAddress('):source.index('    bool nativeOpen()')]
            run(ADDRESS_PREFIX+transact+owner+ADDRESS_CHECKS,a.output/'address-tests',('-DPONG_BLUETOOTH_USERSPACE=1','-I'+str(directory)),cpp=True)

if __name__=='__main__':main()
