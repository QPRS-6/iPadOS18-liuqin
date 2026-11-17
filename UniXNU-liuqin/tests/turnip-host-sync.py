#!/usr/bin/env python3
"""Compile/run the actual exported Darwin host-range sync bodies with recording transport."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mesa',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
source=(a.mesa/'src/freedreno/vulkan/tu_knl_darwin.cc').read_text()
start=source.index('extern "C" __attribute__((visibility("default"))) int32_t\ntu_darwin_sync_host_range')
end=source.index('\nstatic bool\ndw_probe_selector',start)
with tempfile.TemporaryDirectory(prefix='host-sync-',dir=os.environ.get('TMPDIR')) as tmp:
 tmp=Path(tmp);(tmp/'exports.inc').write_text(source[start:end]);binary=tmp/'test'
 cmd=['xcrun','clang++','-std=c++17','-O1','-Wall','-Wextra','-Wmissing-prototypes','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
      '-I'+str(tmp),'-I'+str(a.mesa/'src/freedreno/vulkan'),str(Path(__file__).with_suffix('.cpp')),'-o',str(binary)]
 r=subprocess.run(cmd,capture_output=True,text=True);(a.output_dir/'compile.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit(r.stdout+r.stderr)
 r=subprocess.run([binary],capture_output=True,text=True,timeout=15);(a.output_dir/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
 if r.returncode:raise SystemExit(r.returncode)
