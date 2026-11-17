#!/usr/bin/env python3
"""Run bounded native execution traces; preserve state/ABI, no device execution."""
import argparse,importlib.util,json,struct
from pathlib import Path
from unicorn import UC_HOOK_CODE
from unicorn.arm64_const import (UC_ARM64_REG_X0,UC_ARM64_REG_X14,UC_ARM64_REG_X30,
    UC_ARM64_REG_PC,UC_ARM64_REG_NZCV)

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
scan=load('trace18_scan_fixture',ROOT/'tests/bluetooth-scan-native18.py')
COUNTERS=0x1009e4120

def context(m,phase):
    values=[0xabc00000+n for n in range(19)];values[:3]=[phase,712,1]
    for n,v in enumerate(values):m.uc.reg_write(UC_ARM64_REG_X0+n,v)
    m.uc.reg_write(UC_ARM64_REG_X30,scan.base.STOP);m.uc.reg_write(UC_ARM64_REG_NZCV,0xb0000000)
    return values

def restored(m,values):
    for n,v in enumerate(values):assert m.uc.reg_read(UC_ARM64_REG_X0+n)==v,(n,v)
    assert m.uc.reg_read(UC_ARM64_REG_X30)==scan.base.STOP
    assert m.uc.reg_read(UC_ARM64_REG_NZCV)==0xb0000000;m.restored()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--shared',type=Path,required=True)
    p.add_argument('--patched',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reader=load('trace18_reader',a.shared/'tools/inspect-ios.py');image=reader.Image(a.patched)
    report=json.loads(a.report.read_text());entry=int(report['entries']['scan_execution_trace'],16)
    assert report['trace_counter_storage']['address']==hex(COUNTERS) and report['trace_counter_storage']['per_phase_limit']==2
    stlxr=next(entry+4*n for n in range(80) if image.word(entry+4*n)==0x880efdac)
    cases=0
    for slide in scan.SLIDES:
        for phase in range(4):
            m=scan.ScanMachine(image,image.data,slide);m.flags(13,1);m.write(scan.TABLE+37,b'\xff')
            assert m.read(COUNTERS,16)==bytes(16)
            table=m.read(scan.TABLE,64)
            for call in range(5):
                values=context(m,phase);m.run(entry);restored(m,values);m.selected(1)
                assert m.read(scan.TABLE,64)==table
                assert len(m.scan_messages)==min(call+1,2)
                assert m.scan_messages[-1]==(phase,13,712,1,0xff,1,1)
                assert struct.unpack('<I',m.read(COUNTERS+4*phase,4))[0]==min(call+1,2)
                cases+=1
        for phase in (4,0xffffffff,0x100000000):
            m=scan.ScanMachine(image,image.data,slide);m.flags();values=context(m,phase)
            m.run(entry);restored(m,values);assert not m.scan_messages and m.read(COUNTERS,16)==bytes(16);cases+=1
        m=scan.ScanMachine(image,image.data,slide);m.flags();tries=[]
        def contention(uc,address,size,_):
            if address==stlxr+slide:
                tries.append(address);uc.reg_write(UC_ARM64_REG_X14,1);uc.reg_write(UC_ARM64_REG_PC,address+4)
        m.uc.hook_add(UC_HOOK_CODE,contention);values=context(m,1);m.run(entry);restored(m,values)
        assert len(tries)==4 and not m.scan_messages and m.read(COUNTERS,16)==bytes(16);cases+=1
        m=scan.ScanMachine(image,image.data,slide);m.flags(0,1);m.write(scan.TABLE+37,b'\xff')
        m.run(0x1001c22c0,(0x1001c22c4,));m.restored()
        assert m.scan_messages==[(0,0,0,0,0xff,1,1)];m.selected(0);cases+=1
        m=scan.query_case(image,image.data,slide,0,0,64,0)
        assert m.scan_messages==[(1,0,0,1,0xff,1,1)];cases+=1
        m=scan.ScanMachine(image,image.data,slide);m.flags(0,1);m.write(scan.TABLE+37,b'\xff')
        model=scan.STACK+0x4000;m.uc.mem_write(model+0x2e0,bytes(4));m.uc.reg_write(UC_ARM64_REG_X0,model)
        m.run(0x10032ce54);m.restored();assert m.scan_messages==[(3,0,0,0,0xff,1,1)];cases+=1
        m=scan.ScanMachine(image,image.data,slide);m.flags(0,1);m.write(scan.TABLE+37,b'\xff');m.uc.reg_write(UC_ARM64_REG_X0,0)
        m.run(0x1002585fc);m.restored();assert m.scan_messages==[(2,0,0,0,0xff,1,1)];m.selected(1);cases+=1
    assert cases==112;a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=cases,ASLR_slides=[hex(s) for s in scan.SLIDES],
        per_phase_process_limit=2,exclusive_store_retry_limit=4,scalar_fields_only=True,caller_registers_LR_NZCV_preserved=True,
        hardware_verified=False,scope='Signed native logger/wrapper instructions and guarded loader-zero-fill tail counters; saturation/invalid phase/forced contention and real scan helper scalar fields. No controller or pairing/HID execution.'),indent=2)+'\n')
    print('PASS112 native trace cases /4 ASLR: bounded scalar fields, 2/phase/process, 4 retries, register/LR/NZCV preservation, wrapper correctness; host only.')

if __name__=='__main__':main()
