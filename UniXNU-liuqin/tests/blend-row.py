#!/usr/bin/env python3
"""Check the actual proposed fast row against the existing pixel equation."""
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
source = Path('/Users/qprs/liuqin-unixnu/device-tree/.cache/xnu-work/iokit/Kernel/MiniIOMobileFramebuffer.cpp').read_text()
start = source.index('static inline void\npongBlendPixel')
brace = source.index('{', start)
depth = 0
for end in range(brace, len(source)):
    depth += (source[end] == '{') - (source[end] == '}')
    if not depth:
        pixel = source[start:end + 1]
        break
patch = next((repo / 'patches/xnu-ipados18').glob('*-pong-blend-row-fast.patch')).read_text()
added = '\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+') and not line.startswith('+++'))
begin = added.index('static void\npongBlendRow')
last = '\twhile (count--) pongBlendPixel(to++, *from++);'
row = added[begin:added.index(last) + len(last)] + '\n}\n'
test = r'''
int main() {
    uint32_t seed=17;
    for (unsigned n: {0u,1u,3u,4u,5u,17u,1800u}) {
        for (unsigned mode=0;mode<3;mode++) {
            std::vector<uint32_t> src(n),dst(n+2,0x12345678u);
            for (auto &p:src) {
                seed=seed*1664525u+1013904223u;
                p=mode==0 ? seed|0xff000000u : mode==1 ? seed&0xffffffu : seed;
            }
            auto expected=dst;
            for (unsigned i=0;i<n;i++) pongBlendPixel(&expected[i+1],src[i]);
            pongBlendRow(dst.data()+1,src.data(),n);
            assert(dst==expected);
        }
    }
    puts("PASS: opaque, transparent, mixed alpha and row bounds; original BGRA output retained");
}
'''
with tempfile.TemporaryDirectory(prefix='blend-row-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    cpp = temp / 'test.cpp'
    cpp.write_text('#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n#include <stdio.h>\n#include <vector>\n' + pixel + '\n' + row + test)
    subprocess.run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
