#!/usr/bin/env python3
"""Synthetic sparse-window extraction regression, not an image audit."""
import importlib.util,os,struct,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('window',root/'scripts/extract-pong-simg-window.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='window-test-',dir=os.environ.get('TMPDIR')) as tmp:
 tmp=Path(tmp);source=tmp/'input.simg'
 chunks=[(0xcac1,2,b'abcdefgh'),(0xcac2,1,b'1234'),(0xcac3,2,b''),(0xcac4,0,b'xxxx'),(0xcac1,1,b'WXYZ')]
 data=struct.pack('<IHHHHIIII',0xed26ff3a,1,0,32,16,4,6,len(chunks),0)+b'HEAD'
 for kind,count,payload in chunks:data+=struct.pack('<HHII',kind,0,count,16+len(payload))+b'CHNK'+payload
 source.write_bytes(data);expected=b'abcdefgh1234'+b'\0'*8+b'WXYZ'
 for i,(offset,length) in enumerate([(0,24),(3,17),(9,14),(12,8),(21,3)]):
  out=tmp/f'out{i}';module.extract(source,out,offset,length);assert out.read_bytes()==expected[offset:offset+length]
 try:module.extract(source,tmp/'out0',0,4)
 except ValueError:pass
 else:raise AssertionError('Existing output overwritten')
 try:module.extract(source,tmp/'bad',20,8)
 except ValueError:pass
 else:raise AssertionError('Out-of-range extraction accepted')
 print('PASS raw/fill/hole/chunk-header extensions, arbitrary window offsets, checksum skipping and output/bounds protection')
