#!/usr/bin/env python3
"""Compile actual Mesa-facing adapter bodies around the shipped backend core."""
import argparse,os,shutil,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parents[1];core=r/'drivers/PongAdreno/turnip'
stub=r'''
#pragma once
#include "tu_darwin_core.h"
#include <map>
#include <mutex>
#include <cstdlib>
#include <cstdarg>
#include <cstddef>
enum VkResult {VK_SUCCESS=0,VK_TIMEOUT=2,VK_ERROR_OUT_OF_HOST_MEMORY=-1,VK_ERROR_OUT_OF_DEVICE_MEMORY=-2,VK_ERROR_DEVICE_LOST=-4,VK_ERROR_MEMORY_MAP_FAILED=-5,VK_ERROR_FEATURE_NOT_PRESENT=-8,VK_ERROR_INVALID_EXTERNAL_HANDLE=-9,VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS=-10};
enum tu_bo_alloc_flags {TU_BO_ALLOC_NO_FLAGS=0,TU_BO_ALLOC_GPU_READ_ONLY=2,TU_BO_ALLOC_REPLAYABLE=4,TU_BO_ALLOC_INTERNAL_RESOURCE=8,TU_BO_ALLOC_IMPLICIT_SYNC=64};
using VkMemoryPropertyFlags=unsigned;
struct vk_object_base{};struct tu_sparse_vma{};struct vk_device{int alloc=0;};
struct tu_bo {uint32_t gem_handle=0;uint64_t size=0,iova=0,unique_id=0,darwin_handle=0,darwin_owner=0;uint32_t darwin_token=0;void *map=nullptr,*darwin_failed_map=nullptr;const char *name=nullptr;int32_t refcnt=0;bool implicit_sync=false,never_unmap=false,darwin_imported=false;vk_object_base *base=nullptr;};
struct Heap {uint64_t next=0,free_size=0;bool alloc_high=false;};
struct tu_device {vk_device vk;tu_darwin_backend *darwin=nullptr;uint64_t va_size=0;std::mutex vma_mutex;pthread_mutex_t submit_mutex;Heap vma;std::map<uint32_t,tu_bo> bos;int dma_bo_lock=0;bool fail_lookup=false;};
struct tu_queue {tu_device *device;int fence;};
struct vk_sync{};struct vk_darwin_sync {vk_sync vk;tu_darwin_sync state;};
struct vk_sync_wait {vk_sync *sync;};struct vk_sync_signal {vk_sync *sync;};
struct tu_u_trace_submission_data {unsigned gpu_ts_offset;};
struct tu_cs_entry {const tu_bo *bo;uint32_t offset,size;};
static constexpr uint64_t os_page_size=4096;
static constexpr unsigned VK_SYSTEM_ALLOCATION_SCOPE_DEVICE=1,TU_MEM_SYNC_CACHE_TO_GPU=0;
#define container_of(p,t,m) reinterpret_cast<t *>(reinterpret_cast<char *>(p)-offsetof(t,m))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define TU_RMV(...) ((void)0)
inline void mtx_lock(std::mutex *m){m->lock();}inline void mtx_unlock(std::mutex *m){m->unlock();}
inline uint64_t util_vma_heap_alloc(Heap *h,uint64_t size,uint64_t){if(size>h->free_size)return 0;auto at=h->next;h->next+=size;h->free_size-=size;return at;}
inline bool util_vma_heap_alloc_addr(Heap *h,uint64_t at,uint64_t size){if(at!=h->next)return false;return util_vma_heap_alloc(h,size,4096)!=0;}
inline uint64_t util_vma_heap_get_max_free_continuous_size(Heap *h){return h->free_size;}
inline tu_bo *tu_device_lookup_bo(tu_device *d,uint32_t id){return d->fail_lookup?nullptr:&d->bos[id];}
inline const char *tu_debug_bos_add(tu_device *,uint64_t,const char *name){return name;}
inline void tu_debug_bos_del(tu_device *,tu_bo *){}inline void tu_dump_bo_init(tu_device *,tu_bo *){}inline void tu_dump_bo_del(tu_device *,tu_bo *){}
inline void u_rwlock_rdlock(int *){}inline void u_rwlock_rdunlock(int *){}
inline bool p_atomic_dec_zero(int32_t *p){return __atomic_sub_fetch(p,1,__ATOMIC_ACQ_REL)==0;}
inline int p_atomic_read(int *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}inline void p_atomic_set(int *p,int value){__atomic_store_n(p,value,__ATOMIC_RELEASE);}
inline VkResult vk_error(tu_device *,VkResult r){return r;}inline VkResult vk_errorf(tu_device *,VkResult r,const char *,...){return r;}
inline VkResult vk_device_set_lost(vk_device *,const char *,...){return VK_ERROR_DEVICE_LOST;}
inline void *vk_zalloc(int *,size_t n,unsigned,unsigned){return calloc(1,n);}inline void vk_free(int *,void *p){free(p);}
inline void tu_bo_sync_cache(tu_device *,tu_bo *,uint32_t,uint32_t,unsigned){}
VkResult tu_darwin_error(tu_device *,int32_t,const char *,VkResult);
VkResult tu_darwin_wait_syncs(tu_device *,const struct vk_sync_wait *,uint32_t,bool,bool,uint64_t);
VkResult tu_darwin_publish_signals(tu_device *,const struct vk_sync_signal *,uint32_t,uint32_t);
'''
source=(core/'tu_knl_darwin.cc').read_text();start=source.index('VkResult tu_darwin_error(');brace=source.index('{',start);depth=0
for end in range(brace,len(source)):
    depth+=(source[end]=='{')-(source[end]=='}')
    if not depth:break
with tempfile.TemporaryDirectory(prefix='turnip-adapter-',dir=os.environ.get('TMPDIR')) as td:
    td=Path(td);(td/'tu_darwin_mesa.h').write_text(stub);(td/'adapter-error.inc').write_text(source[start:end+1])
    (td/'tu_rmv.h').write_text('');(td/'tu_cs.h').write_text('')
    for name in ('tu_darwin_memory.cc','tu_darwin_submit.cc'):shutil.copyfile(core/name,td/name)
    shutil.copyfile(r/'tests/turnip-backend-adapter.cpp',td/'test.cpp')
    binary=td/'test'
    command=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
      '-I'+str(td),'-I'+str(core),'-I'+str(r/'tests'),'-I'+str(r/'drivers/PongAdreno/submit'),str(td/'test.cpp'),
      *[str(core/name) for name in ('tu_darwin_connection.cc','tu_darwin_backend.cc','tu_darwin_sync.cc')],'-o',str(binary)]
    p=subprocess.run(command,text=True,capture_output=True,timeout=60);(a.output_dir/'compile.log').write_text(p.stdout+p.stderr)
    if p.returncode:raise SystemExit(p.stdout+p.stderr)
    p=subprocess.run([binary],text=True,capture_output=True,timeout=30);(a.output_dir/'test.log').write_text(p.stdout+p.stderr)
    print(p.stdout+p.stderr,end='')
    if p.returncode:raise SystemExit(p.returncode)
