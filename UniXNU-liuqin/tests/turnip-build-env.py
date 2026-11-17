#!/usr/bin/env python3
"""Test the builder's actual environment isolation and native sanity executable."""
import os,subprocess,tempfile,textwrap
from pathlib import Path
from types import SimpleNamespace
root=Path(__file__).resolve().parents[1]
source=(root/'scripts/build-pong-turnip.py').read_text()
begin=source.index('    env = dict(os.environ, DEVELOPER_DIR=str(a.developer_dir))')
end=source.index("    env['PATH']",begin)
original=dict(os.environ)
try:
 os.environ.update(IPHONEOS_DEPLOYMENT_TARGET='18.0',SDKROOT='/invalid/iPhoneOS.sdk',TVOS_DEPLOYMENT_TARGET='18.0',WATCHOS_DEPLOYMENT_TARGET='11.0')
 scope=dict(os=os,a=SimpleNamespace(developer_dir='/Applications/Xcode.app/Contents/Developer'))
 exec(textwrap.dedent(source[begin:end]),scope)
 env=scope['env']
 for key in ('IPHONEOS_DEPLOYMENT_TARGET','SDKROOT','TVOS_DEPLOYMENT_TARGET','WATCHOS_DEPLOYMENT_TARGET'):
  assert key not in env,key
 with tempfile.TemporaryDirectory(prefix='turnip-env-',dir=original.get('TMPDIR')) as tmp:
  tmp=Path(tmp);file=tmp/'sanity.c';binary=tmp/'sanity'
  file.write_text('#include <stdio.h>\nint main(void){puts("PASS native macOS sanity executable");return 0;}\n')
  subprocess.run(['xcrun','clang','-Wall','-Wextra','-Werror',str(file),'-o',str(binary)],env=env,check=True,timeout=30)
  subprocess.run([binary],env=env,check=True,timeout=10)
 print('PASS actual Turnip builder isolates iOS SDK/deployment variables from native tools')
finally:
 os.environ.clear();os.environ.update(original)
