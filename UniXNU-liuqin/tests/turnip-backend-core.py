#!/usr/bin/env python3
"""Compile the shipped transport-independent backend with recording native IO."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--thread-sanitizer',action='store_true')
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1];core=root/'drivers/PongAdreno/turnip'
with tempfile.TemporaryDirectory(prefix='turnip-core-',dir=os.environ.get('TMPDIR')) as td:
    binary=Path(td)/'test'
    command=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
      '-fsanitize='+('thread' if a.thread_sanitizer else 'address,undefined'),'-fno-sanitize-recover=all',
      '-I'+str(core),'-I'+str(root/'drivers/PongAdreno/submit'),str(Path(__file__).with_suffix('.cpp')),
      *[str(core/name) for name in ('tu_darwin_connection.cc','tu_darwin_backend.cc','tu_darwin_sync.cc')],'-o',str(binary)]
    r=subprocess.run(command,text=True,capture_output=True,timeout=60);(a.output_dir/'compile.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise SystemExit(r.stdout+r.stderr)
    r=subprocess.run([binary],text=True,capture_output=True,timeout=30);(a.output_dir/'test.log').write_text(r.stdout+r.stderr)
    print(r.stdout+r.stderr,end='')
    if r.returncode:raise SystemExit(r.returncode)
