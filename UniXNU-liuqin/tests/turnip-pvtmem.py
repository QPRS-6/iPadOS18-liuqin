#!/usr/bin/env python3
"""Run the actual Turnip private-memory helper under injected BO failures; no GPU."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import os
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mesa',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
p.add_argument('--source-file',type=Path,help='Override only the source of tu_setup_pvtmem for baseline regression')
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
source=(a.source_file or a.mesa/'src/freedreno/vulkan/tu_shader.cc').read_text()
begin=source.index('static VkResult\ntu_setup_pvtmem(')
end=source.index('\nstatic uint64_t\ntu_upload_variant',begin)
helper=source[begin:end]
fixture=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <mutex>
#include <cstdlib>
#undef assert
#define assert(expr) do {if(!(expr)){fprintf(stderr,"FAIL invariant: %s\n",#expr);fflush(stderr);std::_Exit(1);}} while(0)
using VkResult=int;
#define VK_SUCCESS 0
#define TU_BO_ALLOC_INTERNAL_RESOURCE 0
struct tu_bo {unsigned refs;uint64_t iova;};
struct tu_pvtmem_config {uint64_t iova;uint32_t per_fiber_size,per_sp_size;bool per_wave;};
struct tu_pvtmem_bo {std::mutex mtx;tu_bo *bo=nullptr;uint32_t per_fiber_size=0,per_sp_size=0;};
struct Info {uint32_t fibers_per_sp=8,num_sp_cores=4;};
struct Physical {Info *info;};
struct tu_device {Physical *physical_device;tu_pvtmem_bo wave_pvtmem_bo,fiber_pvtmem_bo;};
struct tu_shader {tu_bo *pvtmem_bo=nullptr;};
static unsigned allocations=0,finishes=0;static bool fail=false;
static uint32_t align(uint32_t n,uint32_t a){return (n+a-1)&~(a-1);}
static uint32_t util_next_power_of_two(uint32_t n){uint32_t p=1;while(p<n)p*=2;return p;}
static void mtx_lock(std::mutex *m){m->lock();}
static void mtx_unlock(std::mutex *m){m->unlock();}
static VkResult tu_bo_init_new(tu_device *,void *,tu_bo **out,uint32_t,unsigned,const char *) {
 allocations++;if(fail){*out=nullptr;return -2;}
 *out=new tu_bo{1,0x10000ull*allocations};return 0;
}
static void tu_bo_finish(tu_device *,tu_bo *bo){assert(bo && bo->refs);finishes++;if(!--bo->refs)delete bo;}
static tu_bo *tu_bo_get_ref(tu_bo *bo){assert(bo && bo->refs);bo->refs++;return bo;}
'''
fixture=fixture.replace('#include <mutex>','#include <mutex>\n#include <string.h>')
checks=r'''
int main(){
 Info info;Physical physical{&info};tu_device dev;dev.physical_device=&physical;
 tu_shader s;tu_pvtmem_config cfg{};
 fail=true;
 assert(tu_setup_pvtmem(&dev,&s,&cfg,512,false)==-2);
 assert(dev.fiber_pvtmem_bo.bo==nullptr && dev.fiber_pvtmem_bo.per_fiber_size==0 && finishes==0);
 fail=false;
 assert(tu_setup_pvtmem(&dev,&s,&cfg,512,false)==0 && allocations==2);
 tu_bo *old=s.pvtmem_bo;assert(old->refs==2);
 const auto oldIova=cfg.iova;
 tu_shader failed;
 fail=true;
 assert(tu_setup_pvtmem(&dev,&failed,&cfg,2048,false)==-2);
 assert(dev.fiber_pvtmem_bo.bo==old && old->refs==2 && dev.fiber_pvtmem_bo.per_fiber_size==512 && finishes==0);
 assert(failed.pvtmem_bo==nullptr);
 tu_shader reused;
 assert(tu_setup_pvtmem(&dev,&reused,&cfg,256,false)==0 && cfg.iova==oldIova && allocations==3);
 fail=false;tu_shader grown;
 assert(tu_setup_pvtmem(&dev,&grown,&cfg,2048,false)==0 && allocations==4);
 assert(grown.pvtmem_bo!=old && old->refs==2 && dev.fiber_pvtmem_bo.per_fiber_size==2048);
 tu_shader wave;
 assert(tu_setup_pvtmem(&dev,&wave,&cfg,512,true)==0 && cfg.per_wave);
 assert(wave.pvtmem_bo!=grown.pvtmem_bo);
 tu_shader zero;assert(tu_setup_pvtmem(&dev,&zero,&cfg,0,false)==0 && cfg.iova==0);
 for(tu_bo *bo:{old,reused.pvtmem_bo,grown.pvtmem_bo,wave.pvtmem_bo,dev.fiber_pvtmem_bo.bo,dev.wave_pvtmem_bo.bo})tu_bo_finish(&dev,bo);
 puts("PASS initial OOM/retry, failed growth retains old BO and capacity, successful growth, references, wave/fiber isolation and zero memory");
}
'''
with tempfile.TemporaryDirectory(prefix='pvtmem-test-',dir=os.environ.get('TMPDIR')) as temp:
 temp=Path(temp);file=temp/'test.cpp';file.write_text(fixture+helper+checks)
 binary=temp/'test'
 cmd=['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(file),'-o',str(binary)]
 result=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (a.output_dir/'compile.log').write_text(result.stdout)
 if result.returncode:raise SystemExit(result.stdout)
 result=subprocess.run([binary],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15)
 (a.output_dir/'test.log').write_text(result.stdout);print(result.stdout,end='')
 if result.returncode:raise SystemExit(result.returncode)
