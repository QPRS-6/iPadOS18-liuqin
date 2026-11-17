#!/usr/bin/env python3
"""Replay observed0->ffff registration with stable native model identity; host only."""
import argparse,importlib.util,json,struct
from pathlib import Path
from unicorn.arm64_const import UC_ARM64_REG_X0,UC_ARM64_REG_X30
ROOT=Path(__file__).resolve().parents[1]
def load(n,p):
    s=importlib.util.spec_from_file_location(n,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
scan=load('model18_scan_fixture',ROOT/'tests/bluetooth-scan-native18.py')
life=load('model18_lifecycle_fixture',ROOT/'tests/bluetooth-scan-lifecycle18.py')

def check_model(m,model_type):
    # Run the original singleton getter's resolved path, not a guessed lookup.
    m.uc.reg_write(UC_ARM64_REG_X30,scan.base.STOP);m.run(0x100007880);m.restored()
    pointer=m.uc.reg_read(UC_ARM64_REG_X0)
    if model_type is None:assert pointer==0
    else:assert pointer and struct.unpack('<I',m.uc.mem_read(pointer+scan.MODEL_PRODUCT,4))[0]==model_type

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--shared',type=Path,required=True)
    p.add_argument('--patched',type=Path,required=True);p.add_argument('--baseline90',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reader=load('model18_reader',a.shared/'tools/inspect-ios.py');image=reader.Image(a.patched);old=reader.Image(a.baseline90)
    assert image.uuid==old.uuid=='D3FB9884-D7FB-3B11-A807-5CFB1845C2C8' and image.subtype==0
    counts=dict(gates=0,observed_transition_lifecycle=0,signed90_negatives=0)
    models=(0,1,13,6000,65535,None)
    queries=((0,64,0xff),(0,64,0x20),(0,64,0x40),(1,64,0xff),(0,63,0xff))
    for slide in scan.SLIDES:
        for model_type in models:
            for registered in (0,65535,13):
                for previous in (0,1):
                    for status,count,byte in queries:
                        m=scan.ScanMachine(image,image.data,slide)
                        m.flags(registered,previous,model_type=0 if model_type is None else model_type,model_present=model_type is not None)
                        check_model(m,model_type);identity=m.read(scan.MODEL_POINTER,8)
                        expected=int(status==0 and count>=64 and byte&0x60==0x60) if model_type==0 else previous
                        scan.query_case(image,image.data,slide,registered,status,count,previous,byte=byte,machine=m,expected_selection=expected)
                        assert m.read(scan.CHIP,4)==struct.pack('<I',registered) and m.read(scan.MODEL_POINTER,8)==identity
                        counts['gates']+=1
        for previous in (0,1):
            for registered in (0,65535):
                for status,count,byte in (*queries,(712,64,0xff)):
                    for kind in ('params','disable','enable'):
                        m=scan.ScanMachine(image,image.data,slide);m.flags(0,previous,model_type=0)
                        snapshot=life.cached_getter(m);life.initialize(m,0,previous)
                        # Exact90 device transition is replayed as observed input;
                        # do not claim this write is the native reset's source.
                        m.write(scan.CHIP,struct.pack('<I',registered));check_model(m,0)
                        expected=int(status==0 and count>=64 and byte&0x60==0x60)
                        scan.query_case(image,image.data,slide,registered,status,count,0,byte=byte,machine=m,expected_selection=expected)
                        life.setter(m,snapshot);m.selected(expected)
                        assert m.read(scan.CHIP,4)==struct.pack('<I',registered)
                        scan.dispatch_case(image,image.data,slide,expected,kind,machine=m,scan_type=0)
                        counts['observed_transition_lifecycle']+=1
        m=scan.ScanMachine(old,old.data,slide);m.flags(0,0,model_type=0)
        life.initialize(m,0,0);m.write(scan.CHIP,struct.pack('<I',65535));check_model(m,0)
        scan.query_case(old,old.data,slide,65535,0,64,0,machine=m,expected_selection=0)
        assert m.read(scan.TABLE+37,1)==b'\xff' and m.read(scan.CHIP,4)==struct.pack('<I',65535)
        m.selected(0);assert (1,65535,0,1,0xff,0,0) in m.scan_messages
        scan.dispatch_case(old,old.data,slide,0,'params',machine=m,scan_type=0);counts['signed90_negatives']+=1
    total=sum(counts.values());assert total==1012;a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=total,case_groups=counts,
        ASLR_slides=[hex(s) for s in scan.SLIDES],observed_vendor_transition_replayed=True,vendor_registration_unchanged=True,
        stable_model_native_getter_checked=True,signed90_type0_gate_failure_reproduced=True,
        hardware_verified=False,scope='Native stable singleton/model field and query/config/scan instructions; device-observed0->ffff is replayed, not asserted to be a proven reset write. Null/foreign models, short/error/missing bits and original registration/HCI status retained. No controller TX/reply or physical HID.'),indent=2)+'\n')
    print('PASS1012 stable-model cases /4 ASLR: observed0->ffff no longer loses type0 query selection, registration untouched, null/foreign/status/shape guards preserved; signed90 failure reproduced. Host only.')

if __name__=='__main__':main()
