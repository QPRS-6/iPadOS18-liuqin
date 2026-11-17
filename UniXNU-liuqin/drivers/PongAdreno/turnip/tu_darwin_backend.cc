/* SPDX-License-Identifier: MIT */
#include "tu_darwin_core.h"
#include <new>
#include <string.h>

tu_darwin_backend::tu_darwin_backend(tu_darwin_connection *c, release_range_fn release, void *cookie)
   : conn_(c),release_range_(release),range_cookie_(cookie) { c->retain(); }
int32_t tu_darwin_backend::create(tu_darwin_connection *c, release_range_fn release, void *cookie,
                                 tu_darwin_backend **out)
{
   if (!out) return kIOReturnBadArgument;
   *out=nullptr;
   if (!c || !release) return kIOReturnBadArgument;
   auto *b=new(std::nothrow) tu_darwin_backend(c,release,cookie);
   if (!b) return kIOReturnNoMemory;
   if (pthread_mutex_init(&b->submit_lock_,nullptr)) {delete b;return kIOReturnNoMemory;}
   b->initialized_=1;
   if (pthread_mutex_init(&b->retire_lock_,nullptr)) {delete b;return kIOReturnNoMemory;}
   b->initialized_=2;
   if (pthread_mutex_init(&b->sync_lock_,nullptr)) {delete b;return kIOReturnNoMemory;}
   b->initialized_=3;
   if (pthread_cond_init(&b->sync_cond_,nullptr)) {delete b;return kIOReturnNoMemory;}
   b->initialized_=4;
   NativeAdrenoAbiStatus reply{};
   int32_t status=c->call(NativeAdrenoMethodGetStatus,nullptr,0,&reply,sizeof(reply));
   if (status && status!=kIOReturnUnsupported) {delete b;return status;}
   b->status_supported_=!status;b->initial_faults_=reply.faults;
   if (!c->publish()) {delete b;return kIOReturnBusy;}
   b->published_=true;*out=b;return 0;
}
tu_darwin_backend::~tu_darwin_backend()
{
   shutdown();
   /* Generic CreateDevice failure paths may already have destroyed the VMA.
    * Native close owns final draining; no range callback is made here. */
   if (initialized_>=4) pthread_cond_destroy(&sync_cond_);
   if (initialized_>=3) pthread_mutex_destroy(&sync_lock_);
   if (initialized_>=2) pthread_mutex_destroy(&retire_lock_);
   if (initialized_>=1) pthread_mutex_destroy(&submit_lock_);
   conn_->release();
}
void tu_darwin_backend::shutdown()
{
   if (published_) {conn_->unpublish();published_=false;}
   if (initialized_>=4) fail(kIOReturnAborted,TD_CLOSED);
}
int32_t tu_darwin_backend::fail(int32_t status, tu_darwin_phase phase)
{
   if (!status) status=kIOReturnIOError;
   uint64_t expected=0, value=(uint64_t(phase)<<32)|uint32_t(status);
   pthread_mutex_lock(&sync_lock_);
   bool first=failure_.compare_exchange_strong(expected,value,std::memory_order_acq_rel);
   pthread_cond_broadcast(&sync_cond_);
   pthread_mutex_unlock(&sync_lock_);
   if (first && phase!=TD_CLOSED) tu_darwin_report(conn_->id(),phase,status);
   return failure();
}
bool tu_darwin_backend::contains(uint64_t address, uint64_t bytes) const
{
   const auto &w=conn_->info();
   return bytes && address>=w.va_start && address-w.va_start<w.va_size &&
      bytes<=w.va_size-(address-w.va_start);
}
int32_t tu_darwin_backend::new_bo(uint64_t iova, uint64_t size, void *host, bool readonly, tu_darwin_bo *out)
{
   if (!out) return kIOReturnBadArgument;
   *out={};
   if (!contains(iova,size) || !tu_darwin_range_valid(iova,size)) return kIOReturnBadArgument;
   int32_t status=failure();
   if (!status && (size>conn_->params().max_bo_bytes ||
       (host && (!tu_darwin_range_valid(uintptr_t(host),size) || !(conn_->params().flags&NativeAdrenoParamsHostImport)))))
      status=kIOReturnBadArgument;
   NativeAdrenoAbiBoAllocReply reply{};
   if (!status) {
      if (host) {
         NativeAdrenoAbiBoImport request{uintptr_t(host),size};
         status=conn_->call(NativeAdrenoMethodBoImport,&request,sizeof(request),&reply,sizeof(reply));
      } else {
         NativeAdrenoAbiBoAlloc request{size,12,0};
         status=conn_->call(NativeAdrenoMethodBoAlloc,&request,sizeof(request),&reply,sizeof(reply));
      }
   }
   if (status) {
      release_range_(range_cookie_,iova,size); // no GPU address has been mapped
      if (status==kIOReturnUnderrun) fail(status,TD_ALLOC);
      tu_darwin_report(conn_->id(),TD_ALLOC,status,size,host?1:0);
      return status;
   }
   if (!reply.handle) {release_range_(range_cookie_,iova,size);return fail(kIOReturnUnderrun,TD_ALLOC);}
   tu_darwin_bo bo{conn_->id(),reply.handle,iova,size,host,0,host!=nullptr};
   if (reply.handle>UINT32_MAX) {
      discard_bo(bo);return fail(kIOReturnNoResources,TD_ALLOC);
   }
   NativeAdrenoAbiBoMap request{reply.handle,iova,readonly?1u:3u,0,2,0};
   NativeAdrenoAbiBoMapReply mapped{};
   status=conn_->call(NativeAdrenoMethodBoMap,&request,sizeof(request),&mapped,sizeof(mapped));
   if (!status && mapped.iova!=iova) status=kIOReturnUnderrun;
   if (status) {
      tu_darwin_report(conn_->id(),TD_MAP,status,iova,size);
      discard_bo(bo);return status;
   }
   *out=bo;return 0;
}
int32_t tu_darwin_backend::cleanup_cpu(tu_darwin_bo *bo)
{
   if (!bo->token) return 0;
   if (bo->mapping) {
      int32_t status=conn_->unmap(bo->token,bo->mapping);
      if (status) return status;
      bo->mapping=nullptr;
   }
   NativeAdrenoAbiBoCpuUnmap request{bo->handle,bo->token,0};
   int32_t status=conn_->call(NativeAdrenoMethodBoCpuUnmap,&request,sizeof(request),nullptr,0);
   if (!status) bo->token=0;
   return status;
}
int32_t tu_darwin_backend::map_bo(tu_darwin_bo *bo, void *placed)
{
   if (!bo || bo->owner!=conn_->id() || !bo->handle) return kIOReturnBadArgument;
   if (failure()) return failure();
   if (bo->imported) return !placed || placed==bo->mapping ? 0 : kIOReturnUnsupported;
   if (bo->token) {
      int32_t status=cleanup_cpu(bo);
      if (status) return fail(status,TD_CPU_UNMAP);
   }
   NativeAdrenoAbiBoCpuMap request{bo->handle};
   NativeAdrenoAbiBoCpuMapReply reply{};
   int32_t status=conn_->call(NativeAdrenoMethodBoCpuMap,&request,sizeof(request),&reply,sizeof(reply));
   if (status) return status==kIOReturnUnderrun ? fail(status,TD_CPU_MAP) : status;
   bo->token=reply.token;
   if (!bo->token) return fail(kIOReturnUnderrun,TD_CPU_MAP);
   uint64_t length=0;void *address=nullptr;
   if (reply.bytes<bo->size) status=kIOReturnUnderrun;
   else status=conn_->map(bo->token,placed,&address,&length);
   if (!status) {
      bo->mapping=address; // retained for rollback even if its extent is wrong
      if (!address || length<bo->size || (placed && placed!=address)) status=kIOReturnUnderrun;
   }
   if (status) {
      const int32_t cleanup=cleanup_cpu(bo);
      if (cleanup) fail(cleanup,TD_CPU_UNMAP);
      tu_darwin_report(conn_->id(),TD_CPU_MAP,status,bo->size,length);
   }
   return status;
}
uint32_t tu_darwin_backend::last_fence()
{
   pthread_mutex_lock(&submit_lock_);uint32_t last=last_fence_;pthread_mutex_unlock(&submit_lock_);return last;
}
int32_t tu_darwin_backend::discard_bo(const tu_darwin_bo &bo) { return enqueue(bo,0); }
int32_t tu_darwin_backend::retire_bo(const tu_darwin_bo &bo) { return enqueue(bo,last_fence()); }
int32_t tu_darwin_backend::enqueue(tu_darwin_bo bo, uint32_t fence)
{
   if (bo.owner!=conn_->id() || !bo.handle || !contains(bo.iova,bo.size)) return kIOReturnBadArgument;
   pthread_mutex_lock(&retire_lock_);
   collect_locked();
   if (count_==256 && !failure()) {
      pthread_mutex_unlock(&retire_lock_);
      const uint32_t last=last_fence();
      if (last) wait_fence(last,tu_darwin_deadline(conn_->now(),5000000000ULL));
      pthread_mutex_lock(&retire_lock_);collect_locked();
   }
   int32_t status=0;
   if (count_==256) status=fail(kIOReturnNoResources,TD_CAPACITY);
   else {
      retired_[(head_+count_)%256]={bo,fence};count_++;
      status=collect_locked();
   }
   // On failure the kernel still owns the BO; no IOVA or mapping is fabricated free.
   pthread_mutex_unlock(&retire_lock_);return status;
}
int32_t tu_darwin_backend::collect_locked()
{
   if (failure()) return failure();
   while (count_) {
      auto &entry=retired_[head_];
      if (entry.fence) {
         int32_t status=wait_fence(entry.fence,0);
         if (status==kIOReturnTimeout) return 0;
         if (status) return status;
      }
      int32_t status=cleanup_cpu(&entry.bo);
      if (status) return fail(status,TD_CPU_UNMAP);
      NativeAdrenoAbiBoFree request{entry.bo.handle};
      status=conn_->call(NativeAdrenoMethodBoFree,&request,sizeof(request),nullptr,0);
      if (status==kIOReturnBusy) return 0;
      if (status) return fail(status,TD_RETIRE);
      release_range_(range_cookie_,entry.bo.iova,entry.bo.size);
      entry={};head_=(head_+1)%256;count_--;
   }
   return 0;
}
int32_t tu_darwin_backend::collect()
{
   pthread_mutex_lock(&retire_lock_);int32_t status=collect_locked();pthread_mutex_unlock(&retire_lock_);return status;
}
int32_t tu_darwin_backend::wait_fence(uint32_t fence, uint64_t deadline)
{
   if (!fence) return kIOReturnBadArgument;
   for (;;) {
      if (failure()) return failure();
      uint64_t now=conn_->now();
      uint64_t slice=deadline==UINT64_MAX ? 100000000ULL : deadline>now ? deadline-now : 0;
      if (slice>100000000ULL) slice=100000000ULL;
      NativeAdrenoAbiWaitFence request{fence,slice};
      int32_t status=conn_->call(NativeAdrenoMethodWaitFence,&request,sizeof(request),nullptr,0);
      if (!status) return 0;
      if (status!=kIOReturnTimeout) return fail(status,TD_WAIT);
      if (!slice || (deadline!=UINT64_MAX && conn_->now()>=deadline)) return kIOReturnTimeout;
   }
}
unsigned tu_darwin_backend::batch_limit() const
{
   return conn_->params().flags&NativeAdrenoParamsSubmitBatch ? NativeAdrenoAbiMaxBatchBuffers : NativeAdrenoAbiMaxBuffers;
}
int32_t tu_darwin_backend::prune_fences_locked()
{
   while (fence_count_) {
      int32_t status=wait_fence(fences_[0],0);
      if (status==kIOReturnTimeout) return 0;
      if (status) return status;
      memmove(fences_,fences_+1,(--fence_count_)*sizeof(fences_[0]));
   }
   return 0;
}
int32_t tu_darwin_backend::submit(const NativeAdrenoAbiSubmitBuffer *buffers, unsigned count,
                                 uint32_t *out, uint64_t deadline)
{
   if (!out) return kIOReturnBadArgument;
   *out=0;
   if (!buffers || !count || count>batch_limit()) return kIOReturnNoResources;
   NativeAdrenoAbiSubmitBatch request{};request.buffer_count=count;
   for (unsigned i=0;i<count;i++) {
      if (!buffers[i].handle || !buffers[i].bytes || (buffers[i].bytes&3) || !contains(buffers[i].iova,buffers[i].bytes))
         return kIOReturnBadArgument;
      request.buffers[i]=buffers[i];
   }
   for (;;) {
      if (failure()) return failure();
      pthread_mutex_lock(&submit_lock_);
      int32_t status=prune_fences_locked();
      if (!status && fence_count_<8) {
         NativeAdrenoAbiSubmitReply reply{};
         const bool batch=conn_->params().flags&NativeAdrenoParamsSubmitBatch;
         status=conn_->call(batch?NativeAdrenoMethodSubmitBatch:NativeAdrenoMethodSubmit,&request,
                            batch?sizeof(request):sizeof(NativeAdrenoAbiSubmit),&reply,sizeof(reply));
         if (!status) {
            if (!reply.fence || reply.fence>UINT32_MAX) status=fail(kIOReturnUnderrun,TD_SUBMIT);
            else {
               last_fence_=uint32_t(reply.fence);fences_[fence_count_++]=last_fence_;*out=last_fence_;
               pthread_mutex_unlock(&submit_lock_);return 0;
            }
         }
      } else if (!status) status=kIOReturnBusy;
      uint32_t oldest=fence_count_ ? fences_[0] : 0;
      pthread_mutex_unlock(&submit_lock_);
      if (status!=kIOReturnBusy) return fail(status,TD_SUBMIT);
      const uint64_t now=conn_->now();
      if (deadline!=UINT64_MAX && now>=deadline) return fail(kIOReturnTimeout,TD_SUBMIT);
      if (oldest) {
         uint64_t slice=tu_darwin_deadline(now,10000000ULL);
         if (deadline<slice) slice=deadline;
         status=wait_fence(oldest,slice);
         if (status && status!=kIOReturnTimeout) return status;
      } else {
         // A different client can occupy the global ring. Give it time rather
         // than declaring device loss after a few microsecond retry attempts.
         uint64_t pause=1000000ULL;
         if (deadline!=UINT64_MAX && deadline-now<pause) pause=deadline-now;
         conn_->sleep(pause);
      }
   }
}
int32_t tu_darwin_backend::check_status()
{
   if (failure()) return failure();
   if (!status_supported_) return 0;
   NativeAdrenoAbiStatus reply{};
   int32_t status=conn_->call(NativeAdrenoMethodGetStatus,nullptr,0,&reply,sizeof(reply));
   if (status) return fail(status,TD_WAIT);
   return reply.faults==initial_faults_ ? 0 : fail(kIOReturnIOError,TD_WAIT);
}
