#!/usr/bin/env python3
"""Check actual SMMU/table policy and high-VA packet construction on the host."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='gpu-wide-va-',dir=os.environ.get('TMPDIR')) as td:
    td=Path(td);objects=[];logs=[]
    # The AArch64-only direct MMIO binder belongs to the kernel build, not a
    # host table/packet test. Keep its architecture guard intact.
    sources=sorted(p for p in (r/'drivers/PongAdreno/ArmSmmu4K').glob('*.c') if not p.name.startswith('._') and p.name!='ArmSmmu4KMmio.c')+[r/'drivers/PongAdreno/submit/AdrenoPM4.c']
    for source in sources:
        target=td/(source.stem+'.o');objects.append(target)
        cmd=['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-c',str(source),'-o',str(target)]
        v=subprocess.run(cmd,text=True,capture_output=True,timeout=60);logs.append(v.stdout+v.stderr)
        if v.returncode:(a.output_dir/'compile.log').write_text(''.join(logs));raise SystemExit(v.stdout+v.stderr)
    binary=td/'test';cmd=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(Path(__file__).with_suffix('.cpp')),*map(str,objects),'-o',str(binary)]
    v=subprocess.run(cmd,text=True,capture_output=True,timeout=60);logs.append(v.stdout+v.stderr);(a.output_dir/'compile.log').write_text(''.join(logs))
    if v.returncode:raise SystemExit(v.stdout+v.stderr)
    v=subprocess.run([binary],text=True,capture_output=True,timeout=30);(a.output_dir/'test.log').write_text(v.stdout+v.stderr);print(v.stdout+v.stderr,end='')
    if v.returncode:raise SystemExit(v.returncode)
