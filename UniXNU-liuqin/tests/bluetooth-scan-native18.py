#!/usr/bin/env python3
"""Execute signed22H374 scan selection and native parsing, not controller replies."""
import argparse,json,struct
from pathlib import Path
from unicorn.arm64_const import (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,
    UC_ARM64_REG_X8,UC_ARM64_REG_X9,UC_ARM64_REG_X10,UC_ARM64_REG_X11,
    UC_ARM64_REG_X12,UC_ARM64_REG_X19,UC_ARM64_REG_X20,UC_ARM64_REG_X21,
    UC_ARM64_REG_X29,UC_ARM64_REG_X30,UC_ARM64_REG_PC,UC_ARM64_REG_SP)
import importlib.util

ROOT=Path(__file__).resolve().parents[1]
TABLE=0x1009e3498
CHIP=0x1009cdd60
MODEL_POINTER=0x1009dae90
MODEL_PRODUCT=0x2e0
UPPER=0x1009db058
LOWER=0x1009d8a06
BACKEND_UPPER=0x1009db1e0
BACKEND_LOWER=0x1009d8a05
SLIDES=(0,0x1000,0x5a000000,0x180000000)

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

base=load('scan18_instruction_fixture',ROOT/'tests/bluetooth-native18.py')
STACK=base.STACK
CALLBACK=STACK+0x500

class ScanMachine(base.Machine):
    def __init__(self,image,data,slide):
        self.query_results=[]
        super().__init__(image,data,slide)
        self.write(0x100959c08,struct.pack('<Q',STACK+0x100))
        self.uc.mem_write(STACK+0x100,struct.pack('<Q',0x1122334455667788))
        self.uc.mem_write(CALLBACK,struct.pack('<I',0xd65f03c0))
    def hook(self,uc,address,size,context):
        if address==CALLBACK:
            self.query_results.append((uc.reg_read(UC_ARM64_REG_X0),uc.reg_read(UC_ARM64_REG_X1)))
            # Record the native callback boundary, not a successful init/HCI reply.
            for reg in range(UC_ARM64_REG_X0,UC_ARM64_REG_X0+19):uc.reg_write(reg,0xdeadbeef)
            uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30));return
        super().hook(uc,address,size,context)
    def flags(self,chip=0,old=1,model_type=None,model_present=True):
        self.write(CHIP,struct.pack('<I',chip))
        model=STACK+0x6000
        self.write(MODEL_POINTER,struct.pack('<Q',model if model_present else 0))
        self.uc.mem_write(model+MODEL_PRODUCT,struct.pack('<I',chip if model_type is None else model_type))
        self.write(UPPER-1,bytes([0x55,old,0x77]));self.write(LOWER-1,bytes([0x66,old,0x88]))
        self.write(BACKEND_UPPER,b'\x00');self.write(BACKEND_LOWER,b'\x00')
    def unchanged_backend(self):
        assert self.read(BACKEND_UPPER,1)==b'\0' and self.read(BACKEND_LOWER,1)==b'\0'
        assert self.read(UPPER-1,1)==b'\x55' and self.read(UPPER+1,1)==b'\x77'
        assert self.read(LOWER+1,1)==b'\x88'
    def selected(self,expected):
        assert self.read(UPPER,1)==self.read(LOWER,1)==bytes([expected])
        self.unchanged_backend()

def helper_cases(image,data):
    cases=0
    for slide in SLIDES:
        for byte in range(256):
            m=ScanMachine(image,data,slide);m.flags();table=bytearray(b'\xa5'*64);table[37]=byte;m.write(TABLE,bytes(table))
            m.uc.reg_write(UC_ARM64_REG_X20,0);m.uc.reg_write(UC_ARM64_REG_X21,STACK+0x2000)
            args=[0x1111111100000000+n for n in range(8)]
            for n,value in enumerate(args):m.uc.reg_write(UC_ARM64_REG_X0+n,value)
            m.run(0x1001c6b48,(0x1001c6b4c,));m.selected(int(byte&0x60==0x60))
            assert m.read(TABLE,64)==table and m.uc.reg_read(UC_ARM64_REG_X0)==0
            for n in range(1,8):assert m.uc.reg_read(UC_ARM64_REG_X0+n)==args[n]
            assert m.uc.reg_read(UC_ARM64_REG_X19)==0x19191919 and m.uc.reg_read(UC_ARM64_REG_X21)==STACK+0x2000
            assert m.uc.reg_read(UC_ARM64_REG_SP)==m.sp and m.uc.reg_read(UC_ARM64_REG_X30)==base.STOP;cases+=1
        for status,pointer in ((1,STACK+0x2000),(0x25d,STACK+0x2000),(701,STACK+0x2000),
                               (712,STACK+0x2000),(0xffffffff,STACK+0x2000),(0,0)):
            m=ScanMachine(image,data,slide);m.flags();m.write(TABLE,b'\xff'*64)
            m.uc.reg_write(UC_ARM64_REG_X20,status);m.uc.reg_write(UC_ARM64_REG_X21,pointer)
            m.run(0x1001c6b48,(0x1001c6b4c,));m.selected(0)
            assert m.uc.reg_read(UC_ARM64_REG_X0)==status and m.read(TABLE,64)==b'\xff'*64;cases+=1
        for chip in (1,15,2000,6000):
            m=ScanMachine(image,data,slide);m.flags(chip,1);m.write(TABLE,b'\0'*64)
            m.uc.reg_write(UC_ARM64_REG_X20,712);m.uc.reg_write(UC_ARM64_REG_X21,0)
            m.run(0x1001c6b48,(0x1001c6b4c,));m.selected(1)
            assert m.uc.reg_read(UC_ARM64_REG_X0)==712;cases+=1
    return cases

def query_case(image,data,slide,chip,status,count,old,byte=0xff,publish=True,machine=None,expected_selection=None):
    m=machine or ScanMachine(image,data,slide)
    if machine is None:m.flags(chip,old)
    m.query_results=[]
    m.write(TABLE,b'\x33'*64);buffer=STACK+0x2000;stream=STACK+0x1800;prefix=3
    payload=bytearray(b'\xa5'*count)
    if count>37:payload[37]=byte
    m.uc.mem_write(buffer,b'\x19\x20\x21'+bytes(payload));shape=bytearray(16)
    struct.pack_into('<Q',shape,0,buffer);struct.pack_into('<H',shape,10,prefix+count)
    struct.pack_into('<H',shape,12,prefix);m.uc.mem_write(stream,bytes(shape))
    m.uc.reg_write(UC_ARM64_REG_X0,status);m.uc.reg_write(UC_ARM64_REG_X1,CALLBACK);m.uc.reg_write(UC_ARM64_REG_X2,stream)
    m.run(0x1001c6acc);m.restored()
    expected_status=status if count>=64 or status else 0x25d
    expected_pointer=buffer+prefix if count>=64 else 0
    assert m.query_results==[(expected_status,expected_pointer)],(chip,status,count,m.query_results,expected_status)
    expected=int(status==0 and count>=64 and byte&0x60==0x60) if not chip and publish else old
    if expected_selection is not None:expected=expected_selection
    m.selected(expected)
    if count<64:assert m.read(TABLE,64)==b'\x33'*64
    else:
        # The original parser also ORs its existing command[14] bit0x10.
        copied=payload[:64];copied[14]|=0x10;assert m.read(TABLE,64)==copied
    return m

def query_cases(image,data):
    cases=0
    for slide in SLIDES:
        for chip in (0,1):
            for status in (0,1,712):
                for count in (0,37,63,64,65):
                    for old in (0,1):query_case(image,data,slide,chip,status,count,old);cases+=1
    return cases

def model_cases(image,data,original):
    cases=0;model=STACK+0x4000
    for slide in SLIDES:
        for chip in (0,1,18,19,25,26,2000,2001,4000,5000,6000,65535):
            for flag in (0,1):
                m=ScanMachine(image,data,slide);m.flags(chip,flag);m.uc.mem_write(model+0x2e0,struct.pack('<I',chip))
                m.uc.reg_write(UC_ARM64_REG_X0,model);m.run(0x10032ce54);m.restored()
                if not chip:expected=flag
                else:
                    old=ScanMachine(original,original.data,slide);old.uc.mem_write(model+0x2e0,struct.pack('<I',chip))
                    old.uc.reg_write(UC_ARM64_REG_X0,model);old.run(0x10032ce54);expected=old.uc.reg_read(UC_ARM64_REG_X0)
                assert m.uc.reg_read(UC_ARM64_REG_X0)==expected;m.selected(flag);cases+=1
    return cases

def dispatch_case(image,data,slide,flag,kind,machine=None,scan_type=1):
    m=machine or ScanMachine(image,data,slide)
    if machine is None:m.flags(0,flag)
    m.write(0x1009d8a04,b'\0');m.write(0x1009d8a03,b'\0')
    m.write(0x1009d89e6,b'\x01');m.write(0x1009d89e5,b'\0')
    if kind=='params':
        frame=m.sp+0x70;m.uc.reg_write(UC_ARM64_REG_X29,frame)
        m.uc.mem_write(frame-0x28,b'\x01');m.uc.mem_write(frame-0x29,bytes([scan_type]))
        m.uc.mem_write(frame-0x2c,struct.pack('<H',0x1e0));m.uc.mem_write(frame-0x2e,struct.pack('<H',0x30))
        m.uc.reg_write(UC_ARM64_REG_X8,0x30);m.uc.reg_write(UC_ARM64_REG_X9,0)
        m.uc.reg_write(UC_ARM64_REG_X10,scan_type);m.uc.reg_write(UC_ARM64_REG_X11,0x1e0);m.uc.reg_write(UC_ARM64_REG_X12,0)
        entry=0x10007ec7c;expected_id=0xd7 if flag else 0xab;expected_op=0x2041 if flag else 0x200b
    elif kind=='disable':entry=0x10006d5e8;expected_id=0xd8 if flag else 0xac;expected_op=0x2042 if flag else 0x200c
    else:entry=0x100089c34;expected_id=0xd8 if flag else 0xac;expected_op=0x2042 if flag else 0x200c
    m.run(entry,(0x10002f40c,0x10008458c,0x10006df00))
    assert m.uc.reg_read(UC_ARM64_REG_PC)==0x10002f40c+slide,'Unexpected multicore/vendor backend'
    command_id=m.uc.reg_read(UC_ARM64_REG_X0);assert command_id==expected_id
    if kind=='params' and flag:
        args=struct.unpack('<6Q',m.uc.mem_read(m.sp,48));assert args[:3]==(1,0,1)
        assert bytes(m.uc.mem_read(args[3],1))==bytes([scan_type])
        assert bytes(m.uc.mem_read(args[4],2))==struct.pack('<H',0x1e0)
        assert bytes(m.uc.mem_read(args[5],2))==struct.pack('<H',0x30)
    elif kind!='params':
        enable,duplicates=struct.unpack('<2Q',m.uc.mem_read(m.sp,16))
        assert enable==(0 if kind=='disable' else 1) and duplicates==1
        if flag:assert bytes(m.uc.mem_read(m.sp+16,32))==bytes(32)
    # Execute the original descriptor lookup too: no opcode mapping is guessed.
    m.uc.reg_write(UC_ARM64_REG_X0,command_id);m.uc.reg_write(UC_ARM64_REG_X30,base.STOP)
    m.run(0x10002e738);descriptor=m.uc.reg_read(UC_ARM64_REG_X0)
    assert struct.unpack('<H',m.uc.mem_read(descriptor+2,2))[0]==expected_op
    return m

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('original',type=Path)
    p.add_argument('--shared',type=Path,required=True);p.add_argument('--patched',type=Path,required=True)
    p.add_argument('--baseline87',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reader=load('scan18_reader',a.shared/'tools/inspect-ios.py');image=reader.Image(a.patched);original=reader.Image(a.original)
    assert image.uuid==original.uuid=='D3FB9884-D7FB-3B11-A807-5CFB1845C2C8' and image.subtype==0
    counts=dict(helper=helper_cases(image,image.data),native_queries=query_cases(image,image.data),models=model_cases(image,image.data,original),dispatch=0)
    for slide in SLIDES:
        for flag in (0,1):
            for kind in ('params','disable','enable'):dispatch_case(image,image.data,slide,flag,kind);counts['dispatch']+=1
    old=reader.Image(a.baseline87)
    # The signed87 parser receives the same successful full command table but
    # does not publish type0 scan selection; its native path stays legacy.
    m=query_case(old,old.data,0,0,0,64,0,publish=False)
    assert m.read(TABLE+37,1)==b'\xff' and m.read(UPPER,1)==m.read(LOWER,1)==b'\0'
    for kind in ('params','disable','enable'):dispatch_case(old,old.data,0,0,kind)
    a.output.mkdir(parents=True,exist_ok=True);total=sum(counts.values())
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=total,case_groups=counts,
        ASLR_slides=[hex(v) for v in SLIDES],baseline87_missing_selection_reproduced=True,
        native_standard_descriptors={'params':'0xd7 -> 0x2041','enable':'0xd8 -> 0x2042'},
        hardware_verified=False,scope='Signed22H374 result/cache/model instructions, complete/short/error native parsing and unchanged direct standard/legacy dispatcher boundaries; no TX, HCI reply, scan, pairing or HID execution'),indent=2)+'\n')
    print(f'PASS native18 scan: {total} instruction cases /4 ASLR; actual parsing, stale/error cache refusal, native0x2041/0x2042 selection and signed87 missing-selection baseline. Host only.')

if __name__=='__main__':main()
