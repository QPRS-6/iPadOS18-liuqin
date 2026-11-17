#!/usr/bin/env python3
"""Run native descriptor/decoder selection; initial signed table, not device state."""
import argparse,importlib.util,json,struct
from pathlib import Path
from unicorn.arm64_const import UC_ARM64_REG_X0
ROOT=Path(__file__).resolve().parents[1]
def load(n,p):
    s=importlib.util.spec_from_file_location(n,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--shared',type=Path,required=True)
    p.add_argument('--patched',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    reader=load('decoder18_reader',a.shared/'tools/inspect-ios.py');fixture=load('decoder18_fixture',ROOT/'tests/bluetooth-scan-native18.py')
    image=reader.Image(a.patched);cases=0
    rows=((0x8f,0x1001,0x1001c6910),(0x90,0x1002,0x1001c6acc),(0xab,0x200b,0x100062844),
          (0xd7,0x2041,0x100062844),(0xd8,0x2042,0x100062844))
    for slide in fixture.SLIDES:
        for identifier,opcode,decoder in rows:
            m=fixture.ScanMachine(image,image.data,slide);m.uc.reg_write(UC_ARM64_REG_X0,identifier)
            m.run(0x10002e738);desc=m.uc.reg_read(UC_ARM64_REG_X0)
            assert struct.unpack('<H',m.uc.mem_read(desc+2,2))[0]==opcode
            m.uc.reg_write(UC_ARM64_REG_X0,desc);m.run(0x10006601c)
            assert m.uc.reg_read(UC_ARM64_REG_X0)==decoder+slide;m.restored();cases+=1
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'report.json').write_text(json.dumps(dict(result='PASS',instruction_cases=cases,ASLR_slides=[hex(s) for s in fixture.SLIDES],
        supported_commands_decoder='0x90/0x1002 -> 0x1001c6acc',hardware_verified=False,
        scope='Original native descriptor lookup and decoder selection on the signed image initial table. Does not prove device callback invocation, runtime descriptor mutations or scan/HID success.'),indent=2)+'\n')
    print('PASS20 native descriptor/decoder cases /4 ASLR: 0x1002 selects the expected complete wrapper; initial signed table only, not device execution.')
if __name__=='__main__':main()
