#!/usr/bin/env python3
"""Run the actual high-VA probe body against actual memory-maintenance guards."""
import argparse,os,re,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
a.output_dir.mkdir(parents=True,exist_ok=True);r=Path(__file__).resolve().parents[1]
source=(r/'drivers/PongAdreno/PongAdrenoCp.cpp').read_text()
memory=(r/'drivers/PongAdreno/submit/NativeAdrenoMemory.cpp').read_text()
def block(text,start):
    brace=text.index('{',start);depth=0
    for end in range(brace,len(text)):
        depth+=(text[end]=='{')-(text[end]=='}')
        if not depth:return text[start:end+1]
    raise RuntimeError('unclosed block')
probe=block(source,source.index('    if(gpu->input_bits==36) {',source.index('IOReturn PongAdrenoCpStart(')))
probe=probe.replace('asm volatile("dsb sy":::"memory");','record_barrier();')
if 'acquireDeviceWrites(4096,4096)' not in probe:raise SystemExit('Review probe regression for changed interface')
old=probe.replace('acquireDeviceWrites(4096,4096)','acquireDeviceWrites(4096,sizeof(uint32_t))')
publish=block(memory,memory.index('IOReturn NativeAdrenoMemory::publishForDevice('))
acquire=block(memory,memory.index('IOReturn NativeAdrenoMemory::acquireDeviceWrites('))
# Recording wrappers keep the production memory guards and behavior intact.
publish=publish.replace('::publishForDevice(', '::publishImpl(')
acquire=acquire.replace('::acquireDeviceWrites(', '::acquireImpl(')
fixture=r'''
#include <IOKit/IOReturn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "PongGpuVaPolicy.h"
#define check(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::_Exit(1);}}while(0)
enum NativeAdrenoCacheOperation {NativeAdrenoClean,NativeAdrenoCleanInvalidate,NativeAdrenoInvalidate};
enum PongProbeState {PongProbePass=1,PongProbeFail=2,PongProbeSkip=3,PongProbeRunning=4};
static bool fail_submit,fail_wait,fail_acquire,wrong_value;
static unsigned submitted,waited,acquired,published,barriers;
static size_t acquire_offset,acquire_bytes;
static PongProbeState result_state;
static void record_barrier(){barriers++;}
static void note(const char *,unsigned,uint64_t=0,uint64_t=0){}
static void PongProbeRecord(unsigned,const char *,PongProbeState state,const char *,uint64_t=0){result_state=state;}
static IOReturn NativeAdrenoMaintainOwnedPages(void *,size_t,NativeAdrenoCacheOperation op){return fail_acquire && op==NativeAdrenoInvalidate?kIOReturnIOError:kIOReturnSuccess;}
class NativeAdrenoMemory {
public:
    alignas(4096) uint32_t data[2048]{};
    void *mapping_=data;
    size_t bytes_=sizeof(data);
    bool prepared_=true,quarantined_=false,imported_=false,owner_uncached_=false,write_combined_=true;
    bool backed()const{return mapping_!=nullptr;}
    void *mapping(){return mapping_;}
    IOReturn maintainImported(size_t,size_t,NativeAdrenoCacheOperation){return kIOReturnSuccess;}
    IOReturn publishImpl(size_t,size_t,bool,bool);
    IOReturn acquireImpl(size_t,size_t);
    IOReturn publishForDevice(size_t offset,size_t bytes,bool writes,bool wc=false){published++;return publishImpl(offset,bytes,writes,wc);}
    IOReturn acquireDeviceWrites(size_t offset,size_t bytes){acquired++;acquire_offset=offset;acquire_bytes=bytes;return acquireImpl(offset,bytes);}
};
struct Ring {IOReturn submit(const uint32_t *,size_t,uint32_t *);};
struct Device {unsigned input_bits=36;NativeAdrenoMemory high_probe;uint64_t high_probe_handle=1;Ring ring;};
static Device *gpu;
IOReturn Ring::submit(const uint32_t *launch,size_t count,uint32_t *fence) {
    submitted++;if(fail_submit)return kIOReturnIOError;
    check(count==4 && launch[1]==0 && launch[2]==1 && launch[3]==4);
    check(gpu->high_probe.data[1]==4096 && gpu->high_probe.data[2]==1);
    gpu->high_probe.data[1024]=gpu->high_probe.data[3]^(wrong_value?1u:0u);*fence=2;
    return kIOReturnSuccess;
}
static bool wait_fence(uint32_t fence){waited++;check(fence==2);return !fail_wait;}
static void reset(Device &device){gpu=&device;fail_submit=fail_wait=fail_acquire=wrong_value=false;submitted=waited=acquired=published=barriers=0;acquire_offset=acquire_bytes=0;result_state=PongProbeRunning;}
'''
functions='static IOReturn old_probe(){uint32_t fence=0;\n'+old+'\nreturn kIOReturnSuccess;}\nstatic IOReturn fixed_probe(){uint32_t fence=0;\n'+probe+'\nreturn kIOReturnSuccess;}\n'
checks=r'''
int main() {
    {Device d;reset(d);check(old_probe()==kIOReturnBadArgument);check(d.high_probe.data[1024]==PongGpuHighProbeValue);
     check(submitted==1 && waited==1 && acquired==1 && acquire_bytes==4 && result_state==PongProbeFail);}
    {Device d;reset(d);check(fixed_probe()==kIOReturnSuccess);check(submitted==1 && waited==1 && acquired==1);
     check(acquire_offset==4096 && acquire_bytes==4096 && result_state==PongProbePass && d.high_probe.data[1024]==PongGpuHighProbeValue);}
    {Device d;reset(d);d.input_bits=32;check(fixed_probe()==kIOReturnSuccess && !published && !submitted && !waited && !acquired);}
    {Device d;reset(d);d.high_probe_handle=0;check(fixed_probe()==kIOReturnNotReady && result_state==PongProbeSkip && !submitted);}
    {Device d;reset(d);d.high_probe.prepared_=false;check(fixed_probe()==kIOReturnNotReady && result_state==PongProbeFail && !submitted && !waited && !acquired);}
    {Device d;reset(d);fail_submit=true;check(fixed_probe()==kIOReturnIOError && !waited && !acquired && result_state==PongProbeFail);}
    {Device d;reset(d);fail_wait=true;check(fixed_probe()==kIOReturnTimeout && !acquired && result_state==PongProbeFail);
     check(d.high_probe.data[1024]==PongGpuHighProbeValue);}
    {Device d;reset(d);d.high_probe.write_combined_=false;fail_acquire=true;
     check(fixed_probe()==kIOReturnIOError && acquired==1 && result_state==PongProbeFail);}
    {Device d;reset(d);wrong_value=true;check(fixed_probe()==kIOReturnIOError && acquired==1 && result_state==PongProbeFail);}
    std::puts("PASS actual probe/memory contract: old4-byte acquisition reproduces false FAIL despite correct CP value/fence; whole-page call passes; prerequisite/publish/submit/fence/acquire/value failures stay failures");
}
'''
with tempfile.TemporaryDirectory(prefix='wide-probe-',dir=os.environ.get('TMPDIR')) as td:
    td=Path(td);cpp=td/'test.cpp';cpp.write_text(fixture+publish+'\n'+acquire+'\n'+functions+checks)
    obj=td/'pm4.o';binary=td/'test'
    c=['xcrun','clang','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-c',str(r/'drivers/PongAdreno/submit/AdrenoPM4.c'),'-o',str(obj)]
    v=subprocess.run(c,text=True,capture_output=True,timeout=60)
    if v.returncode:raise SystemExit(v.stdout+v.stderr)
    c=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(r/'drivers/PongAdreno'),str(cpp),str(obj),'-o',str(binary)]
    v=subprocess.run(c,text=True,capture_output=True,timeout=60);(a.output_dir/'compile.log').write_text(v.stdout+v.stderr)
    if v.returncode:raise SystemExit(v.stdout+v.stderr)
    v=subprocess.run([binary],text=True,capture_output=True,timeout=30);(a.output_dir/'test.log').write_text(v.stdout+v.stderr);print(v.stdout+v.stderr,end='')
    if v.returncode:raise SystemExit(v.returncode)
