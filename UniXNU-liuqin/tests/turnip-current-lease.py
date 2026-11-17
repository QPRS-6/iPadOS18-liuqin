#!/usr/bin/env python3
"""Actual KMD lease selection and CreateDevice VMA initialization regression."""
import argparse,os,re,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mesa',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
source=(a.mesa/'src/freedreno/vulkan/tu_knl_darwin.cc').read_text()
start=source.index('static bool\ndarwin_bind_current_window(');brace=source.index('{',start);depth=0
for end in range(brace,len(source)):
 depth+=(source[end]=='{')-(source[end]=='}')
 if not depth:break
helper=source[start:end+1]
device=(a.mesa/'src/freedreno/vulkan/tu_device.cc').read_text()
heap=re.search(r'if \(physical_device->has_set_iova\) \{\s*mtx_init\(&device->vma_mutex.*?\n   \}',device,re.S).group(0)
fixture=r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define check(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::_Exit(1);}}while(0)
constexpr uint32_t NativeAdrenoAbiVersion=1,NativeAdrenoDeviceClientIova=1;
static uint64_t os_page_size=4096;
struct NativeAdrenoAbiParams {uint32_t abi_version=1,input_bits=32;};
struct NativeAdrenoAbiDeviceInfo {uint64_t va_start=0xb0000000,va_size=128ull<<20;uint32_t chip_id=0x07030002,gmem_bytes=2<<20,flags=1;uint64_t gmem_base=0x100000,uche_trap_base=0x1ffffffff000;};
struct tu_physical_device {bool has_set_iova=true;struct{uint64_t chip_id=0x07030002;}dev_id;uint32_t gmem_size=2<<20;uint64_t gmem_base=0x100000,uche_trap_base=0x1ffffffff000,va_start=0xa8000000,va_size=128ull<<20;};
struct Heap {uint64_t start=0,bytes=0;};
struct tu_device {tu_physical_device *physical_device;uint64_t va_start=0,va_size=0;Heap vma;unsigned vma_mutex=0;};
constexpr unsigned mtx_plain=0;
static void mtx_init(unsigned *v,unsigned){*v=1;}
static void util_vma_heap_init(Heap *h,uint64_t start,uint64_t bytes){*h={start,bytes};}
#define ROUND_DOWN_TO(n,a) ((n)&~((a)-1))
'''
checks=r'''
static void heapInit(tu_device *device) {auto *physical_device=device->physical_device;HEAP}
int main() {
 tu_physical_device physical;NativeAdrenoAbiParams params;NativeAdrenoAbiDeviceInfo current;
 unsigned cases=0;
 for(unsigned probe=2;probe<16;probe++)for(unsigned active=2;active<16;active++) {
  physical.va_start=0x90000000ull+(probe-2)*(128ull<<20);current.va_start=0x90000000ull+(active-2)*(128ull<<20);
  tu_device dev{};dev.physical_device=&physical;dev.va_start=physical.va_start;dev.va_size=physical.va_size;
  check(darwin_bind_current_window(&dev,&params,&current));heapInit(&dev);
  check(dev.vma.start==current.va_start && dev.vma.bytes==current.va_size);
  check(physical.va_start==0x90000000ull+(probe-2)*(128ull<<20));
  uint64_t firstBo=dev.vma.start;check(firstBo>=current.va_start && firstBo+(21ull<<20)<=current.va_start+current.va_size);
  if(probe!=active)check(!(physical.va_start>=current.va_start && physical.va_start+(21ull<<20)<=current.va_start+current.va_size));
  cases++;
 }
 for(unsigned fault=0;fault<8;fault++) {
  current=NativeAdrenoAbiDeviceInfo{};params=NativeAdrenoAbiParams{};tu_device dev{};dev.physical_device=&physical;
  switch(fault){case 0:current.va_start++;break;case 1:current.va_size=0;break;case 2:current.va_size>>=1;break;
   case 3:current.va_start=0xfffff000;break;case 4:params.input_bits=64;break;case 5:current.flags=0;break;
   case 6:params.abi_version=2;break;case 7:current.chip_id=0;break;}
  check(!darwin_bind_current_window(&dev,&params,&current));check(!dev.va_start && !dev.va_size);
 }
 std::printf("PASS %u probe/reopen slot pairs: actual active VMA, old-address rejection, immutable physical facts and malformed-lease refusal\n",cases);
}
'''.replace('HEAP',heap)
with tempfile.TemporaryDirectory(prefix='turnip-lease-',dir=os.environ.get('TMPDIR')) as td:
 td=Path(td);file=td/'test.cpp';file.write_text(fixture+helper+'\n'+checks);binary=td/'test'
 cmd=['xcrun','clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(file),'-o',str(binary)]
 r=subprocess.run(cmd,text=True,capture_output=True,timeout=60);(a.output_dir/'compile.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit(r.stdout+r.stderr)
 r=subprocess.run([binary],text=True,capture_output=True,timeout=15);(a.output_dir/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
 if r.returncode:raise SystemExit(r.returncode)
