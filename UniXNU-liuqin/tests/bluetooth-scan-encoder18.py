#!/usr/bin/env python3
"""Execute native scan encoding and real local queue storage, not HCI replies."""
import argparse,importlib.util,json,struct,subprocess,tempfile
from pathlib import Path
from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X30,UC_ARM64_REG_PC

ROOT=Path(__file__).resolve().parents[1]
TEMP=Path('/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode')
def load(n,p):
    s=importlib.util.spec_from_file_location(n,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
scan=load('encoder18_fixture',ROOT/'tests/bluetooth-scan-native18.py')

class Encoder(scan.ScanMachine):
    def __init__(self,image,data,slide,allocation=True):
        self.allocation=allocation;self.allocated=[]
        super().__init__(image,data,slide)
        self.write(0x1009e36b0,struct.pack('<I',1));self.write(0x1009e3478,struct.pack('<I',2));self.write(scan.TABLE,b'\xff'*64)
        # Original queue configuration and storage, with transmission quiescent.
        self.write(0x1009e3470,struct.pack('<Q',scan.STACK+0x6100));self.uc.mem_write(scan.STACK+0x6102,b'\x04')
        self.write(0x1009cd338,struct.pack('<Q',scan.STACK+0x5000));self.write(0x1009cd328,b'\0')
    def hook(self,uc,address,size,context):
        if address==0x100019444+self.slide:
            n=uc.reg_read(UC_ARM64_REG_X0);assert 0<n<=1024;self.allocated.append(n)
            uc.reg_write(UC_ARM64_REG_X0,scan.STACK+0x8000 if self.allocation else 0)
            uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30));return
        if address in (0x10003157c+self.slide,0x100019628+self.slide):
            uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30));return
        super().hook(uc,address,size,context)

def command(image,slide,phys,own,policy,types,intervals,windows,allocation=True):
    m=Encoder(image,image.data,slide,allocation);s=scan.STACK
    m.uc.mem_write(s+0x4000,bytes(types));m.uc.mem_write(s+0x4020,struct.pack('<'+'H'*len(intervals),*intervals))
    m.uc.mem_write(s+0x4040,struct.pack('<'+'H'*len(windows),*windows))
    m.uc.mem_write(m.sp,struct.pack('<6Q',own,policy,phys,s+0x4000,s+0x4020,s+0x4040))
    m.uc.reg_write(UC_ARM64_REG_X0,0xd7);m.uc.reg_write(UC_ARM64_REG_X1,scan.CALLBACK)
    m.run(0x10002f40c);m.restored()
    if not allocation:
        assert m.uc.reg_read(UC_ARM64_REG_X0)==0x68 and m.read(0x1009cd32c,4)==bytes(4);return m,None
    assert m.uc.reg_read(UC_ARM64_REG_X0)==0 and m.read(0x1009cd32c,4)==struct.pack('<I',1)
    pointer,length=struct.unpack('<QH',m.uc.mem_read(s+0x5000,10));raw=bytes(m.uc.mem_read(pointer,length))
    # Actual original queue metadata, descriptor id/opcode and decoder/user CB.
    assert struct.unpack('<H',m.uc.mem_read(s+0x5000+0x14,2))[0]==0x2041
    assert struct.unpack('<H',m.uc.mem_read(s+0x5000+0x16,2))[0]==0xd7
    return m,raw

def h4_check(frames,output):
    source=r'''
#include <cassert>
#include "PongBluetoothProtocol.h"
#include "PongBluetoothNativeProtocol.h"
using namespace pongbt;
int main(){
'''
    for raw,accepted in frames:
        source+='{const unsigned char bytes[]={'+','.join(str(v) for v in raw)+'};Parser parser;Packet p;unsigned count=0;'
        source+='for(auto b:bytes)if(parser.feed(b,p))count++;'
        source+=('assert(count==1&&nativeTxAllowed(p)&&p.bytes+1==sizeof(bytes));' if accepted else 'assert(count==0);')+'}\n'
    source+='}\n'
    with tempfile.TemporaryDirectory(prefix='bt92-encoded-h4-',dir=TEMP) as td:
        p=Path(td)/'check.cpp';exe=Path(td)/'check';p.write_text(source)
        cmd=['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'drivers/PongBluetooth'),str(p),'-o',str(exe)]
        c=subprocess.run(cmd,text=True,capture_output=True);(output/'h4-compile.log').write_text(c.stdout+c.stderr);c.check_returncode()
        v=subprocess.run([str(exe)],text=True,capture_output=True,timeout=20);(output/'h4-test.log').write_text(v.stdout+v.stderr);v.check_returncode()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--shared',type=Path,required=True)
    p.add_argument('--patched',type=Path,required=True);p.add_argument('--baseline91',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reader=load('encoder18_reader',a.shared/'tools/inspect-ios.py');image=reader.Image(a.patched);old=reader.Image(a.baseline91)
    cases=0;frames=[]
    rows=((1,0,0,[0],[0x1e0],[0x30]),(1,1,0,[0],[0x1e0],[0x30]),(1,1,1,[1],[0x40],[0x20]),
          (5,1,0,[0,1],[0x1e0,0x100],[0x30,0x40]),(4,0,0,[0],[0x100],[0x40]))
    for slide in scan.SLIDES:
        for phys,own,policy,types,intervals,windows in rows:
            m,raw=command(image,slide,phys,own,policy,types,intervals,windows)
            params=bytes([own,policy,phys])+b''.join(struct.pack('<BHH',t,i,w) for t,i,w in zip(types,intervals,windows))
            expected=b'\x01\x41\x20'+bytes([len(params)])+params
            assert raw==expected and raw[3]==len(raw)-4;frames.append((raw,True));cases+=1
        command(image,slide,1,1,0,[0],[0x1e0],[0x30],allocation=False);cases+=1
        _,raw=command(old,slide,1,1,0,[0],[0x1e0],[0x30])
        assert raw[3]==9 and len(raw)-4==8;frames.append((raw,False));cases+=1
    a.output.mkdir(parents=True,exist_ok=True);h4_check(frames,a.output);assert cases==28
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=cases,ASLR_slides=[hex(s) for s in scan.SLIDES],
        original_queue_storage_executed=True,actual_H4_contract_checked=True,signed91_declared_length_mismatch_reproduced=True,
        SIGSEGV_cause_proven=False,hardware_verified=False,
        scope='Original native generic dispatcher, variable scan encoder, allocation failure and queue store; emitted non-sensitive frames run through actual H4 parser/TX gate. No controller response/TX or exact device crash PC evidence.'),indent=2)+'\n')
    print('PASS28 native scan encoding/queue cases /4 ASLR plus actual H4 parser/TX gate:3+5 bytes per PHY, signed91 9-vs8 mismatch reproduced, allocation errors retained. Host only; device SIGSEGV PC not proven.')

if __name__=='__main__':main()
