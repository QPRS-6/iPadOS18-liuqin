#!/usr/bin/env python3
"""Compose signed native init/query/config/scan paths in one recording machine."""
import argparse,importlib.util,json,struct
from pathlib import Path
from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X8,UC_ARM64_REG_X30

ROOT=Path(__file__).resolve().parents[1]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

scan=load('scan_lifecycle_fixture',ROOT/'tests/bluetooth-scan-native18.py')

def cached_getter(m):
    # Record the real getter with its dispatch-once boundary already completed.
    m.write(0x1009db060,struct.pack('<Q',0xffffffffffffffff))
    m.uc.reg_write(UC_ARM64_REG_X0,scan.STACK+0x4000)
    m.uc.reg_write(UC_ARM64_REG_X30,scan.base.STOP);m.run(0x100044640);m.restored()
    return m.uc.reg_read(UC_ARM64_REG_X0)

def initialize(m,chip,old):
    before=m.read(scan.TABLE,64)
    m.run(0x1001c22c0,(0x1001c22c4,));m.restored()
    assert m.read(scan.UPPER,1)==m.read(scan.LOWER,1)==bytes([old if chip else 0])
    assert m.read(scan.TABLE,64)==before
    assert m.uc.reg_read(UC_ARM64_REG_X8)==0x1009e3000+m.slide
    # Execute the displaced instruction's continuation/table reset too.
    m.run(0x1001c22c4,(0x1001c22f0,));m.restored()
    assert m.read(scan.TABLE+37,1)==b'\0'
    m.unchanged_backend()

def setter(m,value):
    before=m.read(scan.TABLE,64);m.uc.reg_write(UC_ARM64_REG_X0,value)
    m.uc.reg_write(UC_ARM64_REG_X30,scan.base.STOP);m.run(0x1002585fc);m.restored()
    assert m.uc.reg_read(UC_ARM64_REG_X0)==value and m.read(scan.TABLE,64)==before
    m.unchanged_backend()

def compose(image,slide,old,timing,status,count,byte,kind):
    m=scan.ScanMachine(image,image.data,slide);m.flags(0,old)
    snapshot=cached_getter(m);assert snapshot==old
    initialize(m,0,old)
    scan.query_case(image,image.data,slide,0,status,count,0,byte=byte,machine=m)
    selected=int(status==0 and count>=64 and byte&0x60==0x60)
    if timing=='after':snapshot=cached_getter(m);assert snapshot==selected
    setter(m,snapshot);m.selected(selected)
    # Feed the passive/random-address params actually observed in owner88.
    scan.dispatch_case(image,image.data,slide,selected,kind,machine=m,scan_type=0)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--shared',type=Path,required=True)
    p.add_argument('--patched',type=Path,required=True);p.add_argument('--baseline88',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reader=load('scan_lifecycle_reader',a.shared/'tools/inspect-ios.py');image=reader.Image(a.patched);old_image=reader.Image(a.baseline88)
    assert image.uuid==old_image.uuid=='D3FB9884-D7FB-3B11-A807-5CFB1845C2C8' and image.subtype==0
    cases=dict(composed=0,absent_query=0,other_products=0,signed88_negatives=0)
    queries=[(0,64,byte) for byte in (0,0x20,0x40,0x60,0xff)]+[(1,64,0xff),(712,64,0xff),(0,63,0xff)]
    for slide in scan.SLIDES:
        for previous in (0,1):
            for timing in ('before','after'):
                for status,count,byte in queries:
                    for kind in ('params','disable','enable'):
                        compose(image,slide,previous,timing,status,count,byte,kind);cases['composed']+=1
            for kind in ('params','disable','enable'):
                m=scan.ScanMachine(image,image.data,slide);m.flags(0,previous);snapshot=cached_getter(m)
                initialize(m,0,previous);m.write(scan.TABLE,b'\xff'*64)
                setter(m,snapshot);m.selected(0)
                scan.dispatch_case(image,image.data,slide,0,kind,machine=m,scan_type=0);cases['absent_query']+=1
        for chip in (1,15,2000,6000):
            for previous in (0,1):
                for argument in (0,1):
                    m=scan.ScanMachine(image,image.data,slide);m.flags(chip,previous)
                    initialize(m,chip,previous);setter(m,argument)
                    assert m.read(scan.UPPER,1)==bytes([previous]) and m.read(scan.LOWER,1)==bytes([argument])
                    cases['other_products']+=1
        for previous,status,count,expected_lower in ((0,0,64,0),(1,0,63,1)):
            m=scan.ScanMachine(old_image,old_image.data,slide);m.flags(0,previous);snapshot=cached_getter(m)
            # Same machine: real native query publication followed by the old setter.
            scan.query_case(old_image,old_image.data,slide,0,status,count,previous,machine=m)
            setter(m,snapshot);assert m.read(scan.LOWER,1)==bytes([expected_lower])
            current=int(status==0 and count>=64);assert expected_lower!=current
            scan.dispatch_case(old_image,old_image.data,slide,expected_lower,'params',machine=m,scan_type=0)
            cases['signed88_negatives']+=1
    a.output.mkdir(parents=True,exist_ok=True);total=sum(cases.values());assert total==480
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=total,case_groups=cases,
        ASLR_slides=[hex(s) for s in scan.SLIDES],signed88_stale_setter_reproduced=True,
        hardware_verified=False,
        scope='One signed-native recording machine executes real getter, HCI-init table boundary, complete/short/error query parser, core-config setter and standard/legacy dispatcher/descriptor lookup. Passive owner88 parameter shape retained. No controller TX/reply, physical scan/pairing/HID execution; exact device store timing not captured.'),indent=2)+'\n')
    print(f'PASS native18 lifecycle: {total} composed cases /4 ASLR; new initialization clears stale results, actual query selects scan, stale setter cannot undo/re-enable it, other products unchanged. Signed88 failures reproduced; host only.')

if __name__=='__main__':main()
