#!/usr/bin/env python3
"""Run type0 standard scan-op restore, real scan submit and queue; host only."""
import argparse,importlib.util,json,struct
from pathlib import Path
from unicorn.arm64_const import (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X8,
    UC_ARM64_REG_X30,UC_ARM64_REG_PC,UC_ARM64_REG_SP)

ROOT=Path(__file__).resolve().parents[1]
DEST=0x1009cde70
SRC=0x1009632a0
def load(n,p):
    s=importlib.util.spec_from_file_location(n,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
scan=load('op18_fixture',ROOT/'tests/bluetooth-scan-native18.py')

class OpMachine(scan.ScanMachine):
    def __init__(self,image,data,slide):
        super().__init__(image,data,slide)
        self.write(0x1009e36b0,struct.pack('<I',1));self.write(0x1009e3478,struct.pack('<I',2))
        self.write(0x1009e3470,struct.pack('<Q',scan.STACK+0x6100));self.uc.mem_write(scan.STACK+0x6102,b'\x04')
        self.write(0x1009cd338,struct.pack('<Q',scan.STACK+0x5000));self.write(0x1009cd328,b'\0')
        self.write(scan.TABLE,b'\xff'*64)
    def hook(self,uc,address,size,context):
        if address==0x100019444+self.slide:
            n=uc.reg_read(UC_ARM64_REG_X0);assert 0<n<=1024
            uc.reg_write(UC_ARM64_REG_X0,scan.STACK+0x8000);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30));return
        if address in (0x10003157c+self.slide,0x100019628+self.slide):
            uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30));return
        super().hook(uc,address,size,context)

def restore(m,chip):
    """Execute the patched registration site; stop before vendor/epilogue bodies."""
    before=m.read(DEST,0x948)
    m.uc.mem_write(m.sp+0x28,struct.pack('<Q',scan.base.STOP))
    # The imported bzero returns to this harness, so resume after its call.
    word=m.read(0x1002228a8,4);imm=int.from_bytes(word,'little')&0x3ffffff
    imm=imm-(1<<26) if imm&(1<<25) else imm;trampoline=0x1002228a8+imm*4
    m.uc.reg_write(UC_ARM64_REG_X30,trampoline+20)
    m.uc.reg_write(UC_ARM64_REG_X0,chip);m.uc.reg_write(UC_ARM64_REG_X1,scan.STACK+0x3000)
    m.run(0x1002228a8,() if chip==0 else (0x1002228ac,))
    m.uc.reg_write(UC_ARM64_REG_SP,m.sp)
    return before

def submit(m,phys,types,intervals,windows,scan_type=0):
    s=scan.STACK;m.uc.mem_write(s+0x4000,bytes(types))
    m.uc.mem_write(s+0x4020,struct.pack('<'+'H'*len(intervals),*intervals))
    m.uc.mem_write(s+0x4040,struct.pack('<'+'H'*len(windows),*windows))
    m.uc.mem_write(m.sp,struct.pack('<6Q',1,0,phys,s+0x4000,s+0x4020,s+0x4040))
    m.uc.reg_write(UC_ARM64_REG_X0,0xd7);m.uc.reg_write(UC_ARM64_REG_X1,scan.CALLBACK)
    m.run(0x10002f40c)
    assert m.uc.reg_read(UC_ARM64_REG_SP)==m.sp
    return m.uc.reg_read(UC_ARM64_REG_X0)&0xffffffff

def queue(m):
    index=struct.unpack('<I',m.read(0x1009cd32c,4))[0]
    if not index:return None
    pointer,length=struct.unpack('<QH',m.uc.mem_read(scan.STACK+0x5000,10))
    return bytes(m.uc.mem_read(pointer,length))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--shared',type=Path,required=True)
    p.add_argument('--patched',type=Path,required=True);p.add_argument('--baseline92',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reader=load('op18_reader',a.shared/'tools/inspect-ios.py');image=reader.Image(a.patched);old=reader.Image(a.baseline92)
    source=bytes(image.data[image.offset(SRC+0x100):image.offset(SRC+0x110)])
    assert struct.unpack('<Q',source[:8])[0] and struct.unpack('<Q',source[8:])[0]
    cases=0
    for slide in scan.SLIDES:
        for chip in (1,15,2000,6000):
            m=OpMachine(image,image.data,slide);before=restore(m,chip)
            assert (m.uc.reg_read(UC_ARM64_REG_X8)&0xffffffff)==chip-1
            assert m.read(DEST,0x948)==before;cases+=1
        m=OpMachine(image,image.data,slide);restore(m,0)
        assert m.read(DEST+0x100,16)==source
        assert m.read(DEST,0x100)==bytes(0x100)
        assert m.read(DEST+0x110,0x948-0x110)==bytes(0x948-0x110);cases+=1
        for phys,types,intervals,windows,length in (
            (1,[0],[0x1e0],[0x30],8),(2,[1],[0x40],[0x20],8),(3,[0,1],[0x1e0,0x100],[0x30,0x40],13)):
            m=OpMachine(image,image.data,slide);restore(m,0)
            assert submit(m,phys,types,intervals,windows)==0
            raw=queue(m)
            params=bytes([1,0,phys])+b''.join(struct.pack('<BHH',t,i,w) for t,i,w in zip(types,intervals,windows))
            assert raw==b'\x01\x41\x20'+bytes([len(params)])+params and raw[3]==length;cases+=1
        # Signed92 behavior: ops table cleared, no scan command reaches the queue.
        m=OpMachine(old,old.data,slide);m.write(DEST,bytes(0x948))
        assert m.uc.reg_read(UC_ARM64_REG_X0)==0
        m.uc.reg_write(UC_ARM64_REG_X0,0);m.uc.reg_write(UC_ARM64_REG_X1,1);m.uc.reg_write(UC_ARM64_REG_X0+7,1)
        m.uc.reg_write(UC_ARM64_REG_X30,scan.base.STOP);m.run(0x10008458c)
        assert (m.uc.reg_read(UC_ARM64_REG_X0)&0xffffffff)==0xe11 and queue(m) is None;cases+=1
    a.output.mkdir(parents=True,exist_ok=True);assert cases==36
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=cases,ASLR_slides=[hex(s) for s in scan.SLIDES],
        type0_restores_only_standard_scan_ops=True,vendor_slots_remain_cleared=True,
        signed92_missing_op_reproduced=True,extended_scan_submit_executed=True,
        hardware_verified=False,scope='Signed native type0 registration trampoline, real extended scan submit and local queue storage. No controller TX/reply, opening, pairing or HID execution.'),indent=2)+'\n')
    print('PASS36 type0 scan-op cases /4 ASLR: only standard+0x100/+0x108 restored, vendor slots clear, real0x2041 submit/queue complete; signed92 cleared-op E11-with-no-command reproduced. Host only.')

if __name__=='__main__':main()
