#!/usr/bin/env python3
"""Check original GPU bindings against an owner-tested baseline kernel."""
import argparse,json,subprocess,tomllib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-kernel',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();r=Path(__file__).resolve().parents[1]
board=tomllib.loads((r/'board.toml').read_text());guest=board['ipsw']['build']
abi=json.loads((r/f'configs/pong-iogpu-{guest}.json').read_text())
slide=int(board['kernel']['prelink']['slide'],0)
raw=subprocess.check_output(['xcrun','nm','-n',str(a.baseline_kernel)],text=True)
symbols={line.split()[-1]:int(line.split()[0],16) for line in raw.splitlines() if len(line.split())==3}
bindings={'__ZN15IOAcceleratorESC2EPK11OSMetaClass':'constructor',
 '__ZN15IOAcceleratorESD2Ev':'destructor','__ZN15IOAcceleratorES10gMetaClassE':'metaclass',
 '_PongIOAcceleratorMetaConstruct':'metaclass_constructor'}
for symbol,key in bindings.items():
    assert symbols[symbol]==int(abi[key],16)+slide,(symbol,hex(symbols[symbol]))
a.output_dir.mkdir(parents=True,exist_ok=True)
text=f'PASS {len(bindings)} GPU bindings equal baseline symbols for {guest}, board slide {slide:#x}; no GPU executed\n'
(a.output_dir/'test.log').write_text(text);print(text,end='')
