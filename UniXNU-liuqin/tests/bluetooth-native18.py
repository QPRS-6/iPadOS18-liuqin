#!/usr/bin/env python3
"""Run the matched22H374 routing instructions; not controller/pairing evidence."""
import argparse,copy,importlib.util,json,struct
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import (UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X8,
    UC_ARM64_REG_X9,UC_ARM64_REG_X19,UC_ARM64_REG_X20,UC_ARM64_REG_X21,UC_ARM64_REG_X29,
    UC_ARM64_REG_X30,UC_ARM64_REG_PC,UC_ARM64_REG_SP)

STACK=0x20000000
STOP=STACK+0x10000
CONTEXT=0x1234567812345678

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

class Machine:
    def __init__(self,image,data,slide):
        self.slide=slide;self.uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM);self.stops=set();self.bzero=[];self.visited=[];self.private_messages=[];self.init_messages=[];self.main_messages=[];self.optional_messages=[];self.advance_calls=[];self.failure_calls=[];self.scan_messages=[]
        for name,va,size,offset,file_bytes in image.segments:
            if size and name!='__PAGEZERO':
                self.uc.mem_map(va+slide,(size+4095)&~4095)
                self.uc.mem_write(va+slide,data[offset:offset+file_bytes])
        self.uc.mem_map(STACK,0x11000)
        self.sp=STOP-0x100;self.uc.reg_write(UC_ARM64_REG_SP,self.sp);self.uc.reg_write(UC_ARM64_REG_X30,STOP)
        self.uc.reg_write(UC_ARM64_REG_X19,0x19191919);self.uc.reg_write(UC_ARM64_REG_X20,0x20202020);self.uc.reg_write(UC_ARM64_REG_X29,0x29292929)
        self.uc.hook_add(UC_HOOK_CODE,self.hook)
    def hook(self,uc,address,size,_):
        if address==STOP or address in self.stops:uc.emu_stop();return
        self.visited.append(address-self.slide)
        if address==0x10074f160+self.slide:
            va=uc.reg_read(UC_ARM64_REG_X0);n=uc.reg_read(UC_ARM64_REG_X1);self.bzero.append((va,n));uc.mem_write(va,bytes(n))
            # Imported bzero may clobber all caller-saved registers.
            for reg in range(UC_ARM64_REG_X0,UC_ARM64_REG_X0+19):uc.reg_write(reg,0xdeadbeef)
            uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
        elif address==0x10075015c+self.slide:
            pointer=uc.reg_read(UC_ARM64_REG_X0);message=bytearray()
            for at in range(128):
                value=bytes(uc.mem_read(pointer+at,1))
                if value==b'\0':break
                message.extend(value)
            else:raise AssertionError('Unbounded private diagnostic')
            if message.startswith(b'Pong Bluetooth83: native init step'):
                stack=uc.reg_read(UC_ARM64_REG_SP);label,status=struct.unpack('<QQ',uc.mem_read(stack,16));value=bytearray()
                for at in range(64):
                    byte=bytes(uc.mem_read(label+at,1))
                    if byte==b'\0':break
                    value.extend(byte)
                self.init_messages.append((value.decode(),status&0xffffffff))
            elif message.startswith(b'Pong Bluetooth83: native main return1'):
                self.main_messages.append(struct.unpack('<Q',uc.mem_read(uc.reg_read(UC_ARM64_REG_SP),8))[0]&0xffffffff)
            elif message.startswith(b'Pong Bluetooth84: optional0c5b SKIP'):
                assert b'%' not in message;self.optional_messages.append(message.decode())
            elif message.startswith(b'Pong Bluetooth90: scan phase='):
                values=struct.unpack('<7Q',uc.mem_read(uc.reg_read(UC_ARM64_REG_SP),56))
                assert values[0]<4 and values[3]<=1 and values[4]<=255
                self.scan_messages.append(tuple(v&0xffffffff for v in values))
            else:
                assert b'%' not in message;self.private_messages.append(message.decode())
            for reg in range(UC_ARM64_REG_X0,UC_ARM64_REG_X0+19):uc.reg_write(reg,0xdeadbeef)
            uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
        elif address==0x10003987c+self.slide:
            uc.reg_write(UC_ARM64_REG_X0,0);uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
        elif address==0x10028077c+self.slide:
            self.advance_calls.append(uc.reg_read(UC_ARM64_REG_X0));uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
        elif address==0x1002807dc+self.slide:
            self.failure_calls.append((uc.reg_read(UC_ARM64_REG_X0),uc.reg_read(UC_ARM64_REG_X1)));uc.reg_write(UC_ARM64_REG_PC,uc.reg_read(UC_ARM64_REG_X30))
    def run(self,entry,stops=()):
        self.stops={a+self.slide for a in stops};self.uc.emu_start(entry+self.slide,STOP+4,count=2048)
        pc=self.uc.reg_read(UC_ARM64_REG_PC);assert pc==STOP or pc in self.stops,hex(pc);return self
    def write(self,address,data):self.uc.mem_write(address+self.slide,data)
    def read(self,address,n):return bytes(self.uc.mem_read(address+self.slide,n))
    def restored(self):
        assert self.uc.reg_read(UC_ARM64_REG_SP)==self.sp
        for reg,value in ((UC_ARM64_REG_X19,0x19191919),(UC_ARM64_REG_X20,0x20202020),(UC_ARM64_REG_X29,0x29292929)):
            assert self.uc.reg_read(reg)==value

def check(image,data,report):
    cases=0;slides=(0,0x1000,0x5a000000,0x180000000)
    for slide in slides:
        for chip in (0,1,15,1999,2000,3999,4000,5999,6000,6001,65535,0xffffffff):
            m=Machine(image,data,slide);m.write(0x1009c1fdc,b'\xa5'*4);m.write(0x1009ce7c0,b'\xa5'*8)
            m.uc.reg_write(UC_ARM64_REG_X0,chip);m.uc.reg_write(UC_ARM64_REG_X1,CONTEXT);m.run(0x1002229b0)
            valid=chip<=6000;assert m.uc.reg_read(UC_ARM64_REG_X0)==(0 if valid else 101)
            assert m.read(0x1009c1fdc,4)==(struct.pack('<I',chip) if valid else b'\xa5'*4)
            assert m.read(0x1009ce7c0,8)==(struct.pack('<Q',CONTEXT) if valid else b'\xa5'*8);m.restored();cases+=1
        m=Machine(image,data,slide);m.write(0x1009cde68,b'\x66'*8);m.write(0x1009cde70,b'\xa5'*0x948);m.write(0x1009ce7b8,b'\x77'*8)
        m.uc.reg_write(UC_ARM64_REG_X0,0);m.uc.reg_write(UC_ARM64_REG_X1,CONTEXT);m.run(0x10022288c)
        assert m.bzero==[(0x1009cde70+slide,0x948)]
        # type0 keeps every vendor slot clear but restores the build's own
        # two standard scan submit callbacks used by extended scanning.
        assert m.read(0x1009cde70,0x100)==bytes(0x100)
        assert m.read(0x1009cdf80,0x948-0x110)==bytes(0x948-0x110)
        assert m.read(0x1009cdf70,16)==bytes(m.uc.mem_read(0x1009633a0+slide,16))
        assert m.read(0x1009cdd60,4)==bytes(4)
        assert m.read(0x1009cde68,8)==b'\x66'*8 and m.read(0x1009ce7b8,8)==b'\x77'*8;m.restored();cases+=1
        for chip in (1,15,2000,4000,6000,6001):
            m=Machine(image,data,slide);m.uc.reg_write(UC_ARM64_REG_X0,chip);m.uc.reg_write(UC_ARM64_REG_X1,CONTEXT);m.run(0x10022288c,(0x1002228ac,))
            assert m.uc.reg_read(UC_ARM64_REG_X8)==chip-1 and m.uc.reg_read(UC_ARM64_REG_X0)==chip and not m.bzero;cases+=1
        for entry,site,generic,reg in (
            (0x100222b8c,0x100222bac,0x1009ce7e8,UC_ARM64_REG_X9),
            (0x100222c80,0x100222ca0,0x1009ce960,UC_ARM64_REG_X9),
            (0x100222d40,0x100222d60,0x1009ce8a0,UC_ARM64_REG_X8)):
            for chip in (0,1,15,2000,4000,6000):
                m=Machine(image,data,slide);m.write(0x1009cdd60,struct.pack('<I',chip));m.uc.reg_write(UC_ARM64_REG_X0,CONTEXT)
                m.run(entry,(site+4,) if chip else ());assert m.read(generic,8)==struct.pack('<Q',CONTEXT)
                if chip:assert m.uc.reg_read(reg)==chip-1
                else:m.restored()
                cases+=1
        for native_type in (0,1,15,2000,6000):
            m=Machine(image,data,slide);m.uc.reg_write(UC_ARM64_REG_X0,CONTEXT);m.uc.reg_write(UC_ARM64_REG_X1,native_type)
            m.run(0x10032bc44,(0x10032b17c,));assert m.uc.reg_read(UC_ARM64_REG_X0)==CONTEXT and m.uc.reg_read(UC_ARM64_REG_X1)==0;m.restored();cases+=1
        for product_known in (0,1):
            m=Machine(image,data,slide);m.uc.reg_write(UC_ARM64_REG_X0,product_known);m.run(0x10037bb40,(0x10074eecc,))
            assert m.uc.reg_read(UC_ARM64_REG_X0)==0x5d0 and m.uc.reg_read(UC_ARM64_REG_X1)==0x19191919;cases+=1
        for initialized in (0,0xffffffffffffffff):
            for virtual in (0,1):
                m=Machine(image,data,slide);m.write(0x1009db430,struct.pack('<Q',initialized));m.write(0x1009db428,bytes([virtual]));m.uc.reg_write(UC_ARM64_REG_X0,CONTEXT)
                m.run(0x1003e5614);assert m.uc.reg_read(UC_ARM64_REG_X0)==0;m.restored()
                assert m.read(0x1009db430,8)==struct.pack('<Q',initialized) and m.read(0x1009db428,1)==bytes([virtual]);cases+=1
        for record in report['private_log_sites']:
            site=int(record['address'],16);stop=int(record['stop'],16) if record['kind']=='branch_skip' else site+4
            m=Machine(image,data,slide);m.uc.reg_write(UC_ARM64_REG_X0,CONTEXT);m.uc.reg_write(UC_ARM64_REG_X1,0xbadbadbad);m.run(site,(stop,));m.restored()
            assert m.private_messages==([record['message']] if record['kind']=='redacted_fault' else [])
            cases+=1
        for status in (0,1,0x11,0x12,0xffffffff):
            m=Machine(image,data,slide);label=STACK+0x800;m.uc.mem_write(label,b'initCmdOriginalResultCB\0')
            values=[status,label,*range(0x2222,0x2228)]
            for index,value in enumerate(values):m.uc.reg_write(UC_ARM64_REG_X0+index,value)
            m.run(0x1001c1f7c,(0x1001c1f80,))
            assert m.uc.reg_read(UC_ARM64_REG_SP)==m.sp-0x40
            for index,value in enumerate(values):assert m.uc.reg_read(UC_ARM64_REG_X0+index)==value
            assert m.init_messages==([('initCmdOriginalResultCB',status)] if status else [])
            assert m.uc.reg_read(UC_ARM64_REG_X29)==0x29292929 and m.uc.reg_read(UC_ARM64_REG_X30)==STOP;cases+=1
        for status in (1,0x4b5,0xff000001):
            m=Machine(image,data,slide);m.uc.reg_write(UC_ARM64_REG_X20,status);m.run(0x1000b1838,(0x1000b183c,))
            assert m.main_messages==[status] and m.uc.reg_read(UC_ARM64_REG_X0)==1 and m.uc.reg_read(UC_ARM64_REG_X20)==status
            assert m.uc.reg_read(UC_ARM64_REG_SP)==m.sp and m.uc.reg_read(UC_ARM64_REG_X29)==0x29292929 and m.uc.reg_read(UC_ARM64_REG_X30)==STOP;cases+=1
        for status in (0,701,702,705,712,713,717,0xffffffff):
            m=Machine(image,data,slide);chain=STACK+0x1000;m.write(0x1009e3478,struct.pack('<I',1));m.write(0x1009cd2d8,struct.pack('<Q',chain));m.uc.reg_write(UC_ARM64_REG_X0,status)
            m.run(0x1001c51cc);m.restored()
            if status in (0,701):assert m.advance_calls==[chain] and not m.failure_calls
            else:assert m.failure_calls==[(chain,status)] and not m.advance_calls
            assert len(m.optional_messages)==(1 if status==701 else 0);cases+=1
        # Observed Command Disallowed on the legacy advertising-TX-power read must
        # take the build's own skip branch, never call the read helper.
        for flag in (0,1):
            m=Machine(image,data,slide);m.uc.reg_write(UC_ARM64_REG_X21,flag)
            m.run(0x10061f518,(0x10061f520,0x10061f550))
            assert m.uc.reg_read(UC_ARM64_REG_PC)==0x10061f520+slide;cases+=1
        # Identical unknown-command status on a mandatory callback must fail.
        m=Machine(image,data,slide);chain=STACK+0x1000;m.write(0x1009e3478,struct.pack('<I',1));m.write(0x1009cd2d8,struct.pack('<Q',chain));m.uc.reg_write(UC_ARM64_REG_X0,701)
        m.run(0x1001c48a0);m.restored();assert m.failure_calls==[(chain,701)] and not m.advance_calls and not m.optional_messages;cases+=1
    return cases,slides

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('original',type=Path);p.add_argument('--shared',type=Path,required=True);p.add_argument('--patched',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[1];reader=load('native18_reader',a.shared/'tools/inspect-ios.py');patcher=load('native18_patcher',root/'scripts/bluetooth_native18.py')
    original=reader.Image(a.original);data,report=patcher.patch_native(original,reader);signed=reader.Image(a.patched)
    assert signed.uuid==original.uuid and signed.subtype==0
    # Signing may change LINKEDIT, but every actual tested instruction must match.
    for section in original.sections:
        if section['name']=='__text':assert signed.data[section['offset']:section['offset']+section['size']]==data[section['offset']:section['offset']+section['size']]
    for patch in report['patches']:
        va=int(patch['address'],16);assert all(signed.word(va+4*i)==int(w,16) for i,w in enumerate(patch['words']))
    cases,slides=check(signed,signed.data,report)
    negatives=0
    for kind in ('uuid','subtype','instruction','occupied_padding','private_log_call','private_log_literal','init_trace_site','optional_callback_site','legacy_adv_tx_site',
                 'scan_result_site','scan_model_site','scan_bitmap_copy','scan_params_descriptor','scan_enable_descriptor','scan_config_setter','scan_init_boundary',
                 'trace_counter_overlap','trace_writable_segment','stable_model_pointer','stable_model_product','scan_encoder_length','standard_scan_ops_table','optional_report_0x78','optional_report_0x80'):
        wrong=copy.copy(original);wrong.data=bytearray(original.data)
        if kind=='uuid':wrong.uuid='00000000-0000-0000-0000-000000000000'
        elif kind=='subtype':wrong.subtype=2
        elif kind=='trace_counter_overlap':
            wrong.sections=[*original.sections,dict(name='occupied',segment='__DATA',address=patcher.TRACE_COUNTERS,size=16)]
        elif kind=='trace_writable_segment':
            at=32
            for _ in range(struct.unpack_from('<I',wrong.data,16)[0]):
                cmd,size=struct.unpack_from('<II',wrong.data,at)
                if cmd==0x19 and wrong.data[at+8:at+24].rstrip(b'\0')==b'__DATA':
                    struct.pack_into('<I',wrong.data,at+60,1);break
                at+=size
            else:raise AssertionError('Missing native writable segment')
        else:
            site={'instruction':0x10032bc44,'occupied_padding':0x100957aa8,'private_log_call':0x1000b2508,'private_log_literal':0x1007e0089,'init_trace_site':0x1001c1f7c,'optional_callback_site':0x1001c51cc,'legacy_adv_tx_site':0x10061f518,
                  'scan_result_site':0x1001c6b48,'scan_model_site':0x10032ce58,'scan_bitmap_copy':0x1001c6b98,'scan_params_descriptor':0x1009c19a0,'scan_enable_descriptor':0x1009c19b8,
                  'scan_config_setter':0x1002585fc,'scan_init_boundary':0x1001c22c0,
                  'stable_model_pointer':0x100007884,'stable_model_product':0x10032ce54,'scan_encoder_length':0x100029bec,'standard_scan_ops_table':0x1009633a0,'optional_report_0x78':0x1003127b4,'optional_report_0x80':0x1003127e4}[kind]
            wrong.data[original.offset(site)]^=1
        try:patcher.patch_native(wrong,reader)
        except ValueError:negatives+=1
        else:raise AssertionError('Accepted wrong target: '+kind)
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=cases,negative_guards=negatives,ASLR_slides=[hex(v) for v in slides],uuid=original.uuid,
        private_log_sites=len(report['private_log_sites']),scope='Actual22H374/signed92 routing/privacy/optional, stable-model/scan/trace/native encoder length guards; real encoder tested separately, no fabricated HCI reply/feature or device HID evidence',hardware_verified=False),indent=2)+'\n')
    print(f'PASS native18: {cases} instruction cases, {negatives} version/site guards, {len(slides)} ASLR slides. Host only.')

if __name__=='__main__':main()
