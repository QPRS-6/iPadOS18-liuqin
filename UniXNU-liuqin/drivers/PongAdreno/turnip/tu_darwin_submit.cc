/* SPDX-License-Identifier: MIT */
#include "tu_darwin_mesa.h"
#include "tu_cs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct darwin_submission {
   NativeAdrenoAbiSubmitBuffer commands[NativeAdrenoAbiMaxBatchBuffers];
   unsigned count;
   int32_t error;
};
void *tu_darwin_submit_create(tu_device *dev)
{
   return vk_zalloc(&dev->vk.alloc,sizeof(darwin_submission),8,VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}
void tu_darwin_submit_finish(tu_device *dev, void *submit) { vk_free(&dev->vk.alloc,submit); }
static void dump_ib(const tu_cs_entry &entry)
{
   const char *enabled=getenv("TU_LIUQIN_IBDUMP");
   if (!enabled || strcmp(enabled,"1") || !entry.bo->map) return;
   static std::atomic<unsigned> dumped{0};unsigned index=dumped.fetch_add(1,std::memory_order_relaxed);
   if (index>=8) return;
   const auto *words=reinterpret_cast<const uint32_t *>(static_cast<const char *>(entry.bo->map)+entry.offset);
   fprintf(stderr,"LIUQIN IBDUMP %u iova=0x%llx bytes=%u words=%u\n",index,
      (unsigned long long)(entry.bo->iova+entry.offset),entry.size,entry.size/4);
   for (unsigned at=0;at<entry.size/4;at+=8) {
      fprintf(stderr,"IB %03u:",at);
      for (unsigned k=0;k<8 && at+k<entry.size/4;k++) fprintf(stderr," %08x",words[at+k]);
      fprintf(stderr,"\n");
   }
}
void tu_darwin_submit_add_entries(tu_device *dev, void *opaque, tu_cs_entry *entries, unsigned count)
{
   auto *submit=static_cast<darwin_submission *>(opaque);
   if (submit->error) return;
   const unsigned limit=dev->darwin->batch_limit();
   if (count>limit-submit->count) {submit->error=kIOReturnNoResources;return;}
   if (count && !entries) {submit->error=kIOReturnBadArgument;return;}
   for (unsigned i=0;i<count;i++) {
      const auto &entry=entries[i];
      if (!entry.bo || entry.bo->darwin_owner!=dev->darwin->connection()->id() ||
          !entry.bo->darwin_handle || !entry.size || (entry.size&3) || entry.offset>entry.bo->size ||
          entry.size>entry.bo->size-entry.offset || entry.bo->iova>UINT64_MAX-entry.offset ||
          !dev->darwin->contains(entry.bo->iova+entry.offset,entry.size)) {
         submit->error=kIOReturnBadArgument;return;
      }
   }
   for (unsigned i=0;i<count;i++) {
      const auto &entry=entries[i];
      // Publish command words from this submitting CPU before the kernel doorbell.
      tu_bo_sync_cache(dev,const_cast<tu_bo *>(entry.bo),entry.offset,entry.size,TU_MEM_SYNC_CACHE_TO_GPU);
      dump_ib(entry);
      submit->commands[submit->count++]={entry.bo->darwin_handle,entry.bo->iova+entry.offset,entry.size};
   }
}
void tu_darwin_submit_add_bind(tu_device *dev, void *opaque, tu_sparse_vma *vma,
                               uint64_t vma_offset, tu_bo *bo, uint64_t bo_offset, uint64_t size)
{
   (void)dev;(void)vma;(void)vma_offset;(void)bo;(void)bo_offset;(void)size;
   static_cast<darwin_submission *>(opaque)->error=kIOReturnUnsupported;
}
VkResult tu_darwin_queue_submit(tu_queue *queue, void *opaque, struct vk_sync_wait *waits, uint32_t wait_count,
                                 struct vk_sync_signal *signals, uint32_t signal_count,
                                 tu_u_trace_submission_data *trace)
{
   tu_device *dev=queue->device;
   auto *submit=static_cast<darwin_submission *>(opaque);
   if (submit->error) {
      dev->darwin->fail(submit->error,TD_CAPACITY);
      return tu_darwin_error(dev,submit->error,"atomic submission preflight",VK_ERROR_DEVICE_LOST);
   }
   for (unsigned i=0;i<signal_count;i++) {
      auto *sync=container_of(signals[i].sync,vk_darwin_sync,vk);
      if (sync->state.owner!=dev->darwin)
         return tu_darwin_error(dev,kIOReturnBadArgument,"foreign signal",VK_ERROR_DEVICE_LOST);
   }
   if (wait_count) {
      /* vk_queue resolves deferred timeline waits and requires binary waits
       * to be pending before driver_submit. Keep the caller's preparation
       * lock intact: GPU fences already submitted progress independently. */
      VkResult result=tu_darwin_wait_syncs(dev,waits,wait_count,false,true,0);
      if (result==VK_TIMEOUT)
         return tu_darwin_error(dev,kIOReturnNotReady,"dependency not submitted",VK_ERROR_DEVICE_LOST);
      if (result!=VK_SUCCESS) return result;
      result=tu_darwin_wait_syncs(dev,waits,wait_count,false,false,UINT64_MAX);
      if (result!=VK_SUCCESS) return result;
   }
   uint32_t fence=uint32_t(p_atomic_read(&queue->fence));
   if (submit->count) {
      // One accepted native batch, never an arbitrary split inside BR/BV framing.
      auto *connection=dev->darwin->connection();
      int32_t status=dev->darwin->submit(submit->commands,submit->count,&fence,
         tu_darwin_deadline(connection->now(),5000000000ULL));
      if (status) return tu_darwin_error(dev,status,"native submit",VK_ERROR_DEVICE_LOST);
      p_atomic_set(&queue->fence,int32_t(fence));
   }
   VkResult result=tu_darwin_publish_signals(dev,signals,signal_count,fence);
   if (trace) trace->gpu_ts_offset=0;
   return result;
}
VkResult tu_darwin_queue_wait_fence(tu_queue *queue, uint32_t fence, uint64_t timeout)
{
   if (!fence) return VK_SUCCESS;
   auto *dev=queue->device;auto *connection=dev->darwin->connection();
   int32_t status=dev->darwin->wait_fence(fence,tu_darwin_deadline(connection->now(),timeout));
   if (status==kIOReturnTimeout && !dev->darwin->failure()) return VK_TIMEOUT;
   if (!status) status=dev->darwin->collect();
   return tu_darwin_error(dev,status,"queue fence",VK_ERROR_DEVICE_LOST);
}
