#!/usr/bin/env python3
"""Actual Turnip suballocator failure/ownership checks with ASan/UBSan; no GPU."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mesa',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
source=(a.mesa/'src/freedreno/vulkan/tu_suballoc.cc').read_text()
helper=source[source.index('VkResult\ntu_suballoc_bo_alloc('):source.index('\nvoid *\ntu_suballoc_bo_map')]
fixture=r'''
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define assert(e) do{if(!(e)){fprintf(stderr,"FAIL %s\n",#e);std::_Exit(1);}}while(0)
#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
#define MAX2(a,b) ((a)>(b)?(a):(b))
using VkResult=int;
struct tu_device{};
struct tu_bo {unsigned refcnt;uint32_t size;uint64_t iova;};
struct tu_suballoc_bo {tu_bo *bo=nullptr;uint64_t iova=0;uint32_t size=0;};
struct tu_suballocator {tu_device *dev;tu_bo *bo=nullptr,*cached_bo=nullptr;uint32_t next_offset=0,default_size=4096;unsigned flags=0;const char *name="test";};
static int map_error=0;static unsigned allocs=0,maps=0,freed=0;
static uint32_t align(uint32_t n,uint32_t a){return (n+a-1)&~(a-1);}
static unsigned p_atomic_read(unsigned *n){return *n;}
static tu_bo *tu_bo_get_ref(tu_bo *bo){assert(bo && bo->refcnt);bo->refcnt++;return bo;}
static void tu_bo_finish(tu_device *,tu_bo *bo){assert(bo && bo->refcnt);if(!--bo->refcnt){freed++;delete bo;}}
static VkResult tu_bo_init_new(tu_device *,void *,tu_bo **out,uint32_t size,unsigned,const char *){allocs++;*out=new tu_bo{1,size,allocs*0x10000ull};return 0;}
static VkResult tu_bo_map(tu_device *,tu_bo *bo,void *){assert(bo);maps++;return map_error;}
'''
checks=r'''
int main(){
 tu_device dev;tu_suballocator pool;pool.dev=&dev;tu_suballoc_bo out;
 map_error=-7;assert(tu_suballoc_bo_alloc(&out,&pool,128,128)==-7);
 assert(pool.bo==nullptr && pool.next_offset==0 && freed==1 && out.bo==nullptr);
 map_error=0;assert(tu_suballoc_bo_alloc(&out,&pool,128,128)==0 && allocs==2);
 assert(pool.bo==out.bo && out.bo->refcnt==2);
 tu_suballoc_bo second;assert(tu_suballoc_bo_alloc(&second,&pool,64,64)==0 && allocs==2 && second.iova==out.iova+128);
 tu_suballoc_bo_free(&pool,&out);assert(!out.bo && !out.iova && !out.size);
 tu_suballoc_bo_free(&pool,&out); // no double release
 tu_bo_finish(&dev,pool.bo);pool.bo=nullptr;
 tu_bo *saved=second.bo;assert(saved->refcnt==1);
 tu_suballoc_bo_free(&pool,&second);assert(pool.cached_bo==saved && !second.bo);
 tu_suballoc_bo_free(&pool,&second);assert(saved->refcnt==1);
 tu_suballoc_bo reused;assert(tu_suballoc_bo_alloc(&reused,&pool,256,128)==0 && allocs==2 && maps==3);
 tu_suballoc_bo_free(&pool,&reused);tu_bo_finish(&dev,pool.bo);
 assert(freed==2);
 puts("PASS mapping failure clears dangling BO, exact error, retry, packed allocation, ownership transfer, cached reuse and repeated free");
}
'''
with tempfile.TemporaryDirectory(prefix='suballoc-test-',dir=os.environ.get('TMPDIR')) as tmp:
 tmp=Path(tmp);file=tmp/'test.cpp';file.write_text(fixture+helper+checks)
 binary=tmp/'test'
 cmd=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(file),'-o',str(binary)]
 r=subprocess.run(cmd,capture_output=True,text=True);(a.output_dir/'compile.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit(r.stdout+r.stderr)
 r=subprocess.run([binary],capture_output=True,text=True,timeout=15);(a.output_dir/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
 if r.returncode:raise SystemExit(r.returncode)
