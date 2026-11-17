/* SPDX-License-Identifier: MIT */
#include "tu_darwin_mesa.h"
#include "tu_rmv.h"
#include <stdio.h>
#include <string.h>

static tu_darwin_bo record(const tu_bo *bo)
{
   return {bo->darwin_owner,bo->darwin_handle,bo->iova,bo->size,
      bo->map?bo->map:bo->darwin_failed_map,bo->darwin_token,bo->darwin_imported};
}
static VkResult reserve_iova(tu_device *dev, uint64_t size, uint64_t requested,
                              tu_bo_alloc_flags flags, uint64_t *out)
{
   *out=0;
   if (!size || (size&4095)) return vk_error(dev,VK_ERROR_OUT_OF_DEVICE_MEMORY);
   if ((flags&TU_BO_ALLOC_REPLAYABLE) && requested &&
       (!tu_darwin_range_valid(requested,size) || !dev->darwin->contains(requested,size)))
      return VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
   int32_t status=dev->darwin->collect();
   if (status) return tu_darwin_error(dev,status,"collect before allocation",VK_ERROR_DEVICE_LOST);
   mtx_lock(&dev->vma_mutex);
   bool replay=flags&TU_BO_ALLOC_REPLAYABLE;
   if (replay && requested) {
      if (util_vma_heap_alloc_addr(&dev->vma,requested,size)) *out=requested;
   } else {
      dev->vma.alloc_high=replay;
      *out=util_vma_heap_alloc(&dev->vma,size,os_page_size);
   }
   uint64_t available=0,largest=0;
   if (!*out) {available=dev->vma.free_size;largest=util_vma_heap_get_max_free_continuous_size(&dev->vma);}
   mtx_unlock(&dev->vma_mutex);
   if (*out) return VK_SUCCESS;
   tu_darwin_report(dev->darwin->connection()->id(),TD_CAPACITY,kIOReturnNoSpace,size,available);
   static std::atomic<unsigned> reports{0};
   if (reports.fetch_add(1,std::memory_order_relaxed)<16)
      fprintf(stderr,"Pong Turnip63 OOM: device=%llu request=%llu free=%llu largest=%llu window=%llu\n",
         (unsigned long long)dev->darwin->connection()->id(),(unsigned long long)size,
         (unsigned long long)available,(unsigned long long)largest,(unsigned long long)dev->va_size);
   return replay && requested ? VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
static VkResult publish_bo(tu_device *dev, const tu_darwin_bo &native, vk_object_base *base,
                            tu_bo_alloc_flags flags, const char *name, tu_bo **out)
{
   tu_bo *bo=tu_device_lookup_bo(dev,uint32_t(native.handle));
   if (!bo || bo->gem_handle) {
      if (!bo) dev->darwin->discard_bo(native);
      if (bo) dev->darwin->fail(kIOReturnBadArgument,TD_ALLOC);
      return tu_darwin_error(dev,bo?kIOReturnBadArgument:kIOReturnNoMemory,"BO metadata",VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   *bo={};
   bo->gem_handle=uint32_t(native.handle);bo->size=native.size;bo->iova=native.iova;
   bo->name=tu_debug_bos_add(dev,native.size,name);bo->refcnt=1;
   bo->implicit_sync=flags&TU_BO_ALLOC_IMPLICIT_SYNC;bo->base=base;bo->unique_id=native.handle;
   bo->darwin_owner=native.owner;bo->darwin_handle=native.handle;bo->darwin_imported=native.imported;
   bo->map=native.mapping;bo->never_unmap=native.imported;
   tu_dump_bo_init(dev,bo);TU_RMV(bo_allocate,dev,bo);
   if (flags&TU_BO_ALLOC_INTERNAL_RESOURCE) {
      TU_RMV(internal_resource_create,dev,bo);TU_RMV(resource_name,dev,bo,name);
   }
   *out=bo;return VK_SUCCESS;
}
VkResult tu_darwin_bo_init(tu_device *dev, vk_object_base *base, tu_bo **out, uint64_t size,
                            uint64_t requested, VkMemoryPropertyFlags properties,
                            tu_bo_alloc_flags flags, tu_sparse_vma *lazy, const char *name)
{
   (void)properties;*out=nullptr;
   if (lazy) return vk_error(dev,VK_ERROR_FEATURE_NOT_PRESENT);
   uint64_t address=0;
   VkResult result=reserve_iova(dev,size,requested,flags,&address);
   if (result!=VK_SUCCESS) return result;
   tu_darwin_bo native;
   int32_t status=dev->darwin->new_bo(address,size,nullptr,flags&TU_BO_ALLOC_GPU_READ_ONLY,&native);
   if (status) return tu_darwin_error(dev,status,"native BO allocation/map",VK_ERROR_OUT_OF_DEVICE_MEMORY);
   return publish_bo(dev,native,base,flags,name,out);
}
VkResult tu_darwin_bo_init_host(tu_device *dev, tu_bo **out, void *pointer, uint64_t size)
{
   *out=nullptr;
   if (!tu_darwin_range_valid(uintptr_t(pointer),size) ||
       !(dev->darwin->connection()->params().flags&NativeAdrenoParamsHostImport))
      return vk_error(dev,VK_ERROR_INVALID_EXTERNAL_HANDLE);
   uint64_t address=0;
   VkResult result=reserve_iova(dev,size,0,TU_BO_ALLOC_NO_FLAGS,&address);
   if (result!=VK_SUCCESS) return result;
   tu_darwin_bo native;
   int32_t status=dev->darwin->new_bo(address,size,pointer,false,&native);
   if (status) return tu_darwin_error(dev,status,"native host import/map",VK_ERROR_OUT_OF_DEVICE_MEMORY);
   return publish_bo(dev,native,nullptr,TU_BO_ALLOC_NO_FLAGS,"host import",out);
}
VkResult tu_darwin_bo_map(tu_device *dev, tu_bo *bo, void *placed)
{
   tu_darwin_bo native=record(bo);
   int32_t status=dev->darwin->map_bo(&native,placed);
   bo->darwin_token=native.token;
   if (status) {
      // A failed VM mapping may still own a kernel view. Keep it for teardown,
      // but do not publish it as bo->map (which Mesa treats as a successful map).
      bo->darwin_failed_map=native.mapping;
      return tu_darwin_error(dev,status,"CPU mapping",VK_ERROR_MEMORY_MAP_FAILED);
   }
   bo->map=native.mapping;bo->darwin_failed_map=nullptr;bo->never_unmap=true;
   TU_RMV(bo_map,dev,bo);return VK_SUCCESS;
}
void tu_darwin_bo_finish(tu_device *dev, tu_bo *bo)
{
   u_rwlock_rdlock(&dev->dma_bo_lock);
   if (!p_atomic_dec_zero(&bo->refcnt)) {u_rwlock_rdunlock(&dev->dma_bo_lock);return;}
   const tu_darwin_bo native=record(bo);
   tu_debug_bos_del(dev,bo);tu_dump_bo_del(dev,bo);
   if (bo->map && bo->darwin_token) TU_RMV(bo_unmap,dev,bo);
   TU_RMV(bo_destroy,dev,bo);
   memset(bo,0,sizeof(*bo));
   u_rwlock_rdunlock(&dev->dma_bo_lock);
   dev->darwin->retire_bo(native);
}
VkResult tu_darwin_bo_init_dmabuf(tu_device *dev, tu_bo **out, uint64_t size, tu_bo_alloc_flags flags, int fd)
{
   (void)size;(void)flags;(void)fd;*out=nullptr;return vk_error(dev,VK_ERROR_FEATURE_NOT_PRESENT);
}
int tu_darwin_bo_export_dmabuf(tu_device *dev, tu_bo *bo) { (void)dev;(void)bo;return -1; }
void tu_darwin_bo_allow_dump(tu_device *dev, tu_bo *bo) { (void)dev;(void)bo; }
void tu_darwin_bo_set_metadata(tu_device *dev, tu_bo *bo, void *metadata, uint32_t size)
{
   if (size>NativeAdrenoAbiMetadataBytes || (size && !metadata)) return;
   NativeAdrenoAbiMetadata request{};request.handle=bo->darwin_handle;request.bytes=size;
   if (size) memcpy(request.text,metadata,size);
   dev->darwin->connection()->call(NativeAdrenoMethodSetMetadata,&request,sizeof(request),nullptr,0);
}
int tu_darwin_bo_get_metadata(tu_device *dev, tu_bo *bo, void *metadata, uint32_t size)
{
   if (size && !metadata) return -1;
   NativeAdrenoAbiMetadata request{},reply{};request.handle=bo->darwin_handle;
   request.bytes=MIN2(size,NativeAdrenoAbiMetadataBytes);
   int32_t status=dev->darwin->connection()->call(NativeAdrenoMethodGetMetadata,&request,sizeof(request),&reply,sizeof(reply));
   if (status || reply.bytes>request.bytes || reply.bytes>sizeof(reply.text)) return -1;
   if (reply.bytes) memcpy(metadata,reply.text,reply.bytes);
   return 0;
}
