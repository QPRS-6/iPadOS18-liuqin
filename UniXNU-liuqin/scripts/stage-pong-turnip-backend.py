#!/usr/bin/env python3
"""Stage the device-owned Darwin backend into a prepared Mesa26.2.6 tree."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
FILES=('tu_darwin_core.h','tu_darwin_host_sync.h','tu_darwin_connection.cc',
       'tu_darwin_backend.cc','tu_darwin_sync.cc','tu_darwin_iokit.cc',
       'tu_darwin_mesa.h','tu_darwin_memory.cc','tu_darwin_mesa_sync.cc',
       'tu_darwin_submit.cc','tu_knl_darwin.cc')
def stage(mesa):
    source=ROOT/'drivers/PongAdreno/turnip'
    dest=mesa/'src/freedreno/vulkan'
    prior=mesa/'.cache/pong-turnip-backend-source'
    patch=ROOT/'patches/mesa/0064-pong-darwin-backend-v2-integration.patch'
    marker='tu_darwin_backend *darwin;'
    for name in FILES:
        target=dest/name; saved=prior/name; current=source/name
        if saved.exists() and target.exists() and target.read_bytes() not in (saved.read_bytes(),current.read_bytes()):
            raise SystemExit('Preserving direct Mesa edits; reconcile with canonical source: '+str(target))
        if not saved.exists() and target.exists() and name not in ('tu_knl_darwin.cc','tu_darwin_host_sync.h'):
            if target.read_bytes()!=current.read_bytes():raise SystemExit('Unexpected existing backend source: '+str(target))
    if marker not in (dest/'tu_device.h').read_text():
        subprocess.run(['git','apply','--check',str(patch)],cwd=mesa,check=True)
        subprocess.run(['git','apply',str(patch)],cwd=mesa,check=True)
    else:
        subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=mesa,check=True)
    prior.mkdir(parents=True,exist_ok=True)
    for name in FILES:
        shutil.copyfile(source/name,dest/name)
        shutil.copyfile(source/name,prior/name)
    print('Staged per-device Darwin backend:',source)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mesa',type=Path,required=True)
    stage(parser.parse_args().mesa.resolve())
