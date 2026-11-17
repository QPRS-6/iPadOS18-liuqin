/* SPDX-License-Identifier: MIT
 * PONG_TURNIP_BACKEND_V2: NativeAdreno ABI1 adapter for Mesa Turnip.
 * Connection, BO retirement, submit admission and sync state are per device.
 * Canonical sources: UniXNU-liuqin/drivers/PongAdreno/turnip.
 */
#include "tu_darwin_mesa.h"
#include "tu_dynamic_rendering.h"
#include "util/u_debug.h"
#include "vk_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

VkResult tu_darwin_error(tu_device *dev, int32_t status, const char *what, VkResult fallback)
{
   if (!status) return VK_SUCCESS;
   if (dev->darwin && (dev->darwin->failure() || fallback==VK_ERROR_DEVICE_LOST)) {
      if (!dev->darwin->failure()) dev->darwin->fail(status,TD_WAIT);
      return vk_device_set_lost(&dev->vk,"%s: native=0x%x phase=%u connection=%llu",what,uint32_t(status),
         unsigned(dev->darwin->failure_phase()),(unsigned long long)dev->darwin->connection()->id());
   }
   return vk_errorf(dev,fallback,"%s: native=0x%x",what,uint32_t(status));
}
static void release_range(void *cookie, uint64_t address, uint64_t bytes)
{
   auto *dev=static_cast<tu_device *>(cookie);
   mtx_lock(&dev->vma_mutex);util_vma_heap_free(&dev->vma,address,bytes);mtx_unlock(&dev->vma_mutex);
}
static VkResult device_init(tu_device *dev)
{
   dev->fd=-1;dev->darwin=nullptr;
   tu_darwin_connection *connection=nullptr;
   int32_t status=tu_darwin_connection::create(tu_darwin_iokit_io(),&connection);
   if (status) return vk_errorf(dev,VK_ERROR_INITIALIZATION_FAILED,"native device open: 0x%x",uint32_t(status));
   const auto &info=connection->info();
   const auto *physical=dev->physical_device;
   if (info.chip_id!=physical->dev_id.chip_id || info.gmem_base!=physical->gmem_base ||
       info.uche_trap_base!=physical->uche_trap_base) {
      connection->release();return vk_error(dev,VK_ERROR_INITIALIZATION_FAILED);
   }
   dev->va_start=info.va_start;dev->va_size=info.va_size;
   status=tu_darwin_backend::create(connection,release_range,dev,&dev->darwin);
   if (!status) fprintf(stderr,"Pong Turnip63 device: connection=%llu probe=0x%llx active=0x%llx bytes=%llu\n",
      (unsigned long long)connection->id(),(unsigned long long)physical->va_start,
      (unsigned long long)info.va_start,(unsigned long long)info.va_size);
   connection->release();
   return status ? vk_errorf(dev,VK_ERROR_INITIALIZATION_FAILED,"native device state: 0x%x",uint32_t(status)) : VK_SUCCESS;
}
static void device_finish(tu_device *dev)
{
   delete dev->darwin;dev->darwin=nullptr;
}
static int get_timestamp(tu_device *dev, uint64_t *out)
{
   NativeAdrenoAbiTimestamp reply{};
   int32_t status=dev->darwin->connection()->call(NativeAdrenoMethodGetTimestamp,nullptr,0,&reply,sizeof(reply));
   if (status) return -1;
   *out=reply.ticks;return 0;
}
static int get_suspend_count(tu_device *dev, uint64_t *out)
{
   NativeAdrenoAbiStatus reply{};
   int32_t status=dev->darwin->connection()->call(NativeAdrenoMethodGetStatus,nullptr,0,&reply,sizeof(reply));
   if (status) return -1;
   *out=reply.suspends;return 0;
}
static VkResult check_status(tu_device *dev)
{
   return tu_darwin_error(dev,dev->darwin->check_status(),"device status",VK_ERROR_DEVICE_LOST);
}
static int queue_new(tu_device *dev, tu_queue *queue) { (void)dev;queue->msm_queue_id=0;return 0; }
static void queue_close(tu_device *dev, tu_queue *queue) { (void)dev;(void)queue; }
static VkResult sparse_init(tu_device *dev, vk_object_base *base, tu_sparse_vma *vma, uint64_t *out,
                             tu_sparse_vma_flags flags, uint64_t size, uint64_t address)
{
   (void)base;(void)vma;(void)flags;(void)size;(void)address;if(out)*out=0;
   return vk_error(dev,VK_ERROR_FEATURE_NOT_PRESENT);
}
static void sparse_finish(tu_device *dev, tu_sparse_vma *vma) { (void)dev;(void)vma; }

static const tu_knl darwin_knl_funcs={
   .name="darwin",.device_init=device_init,.device_finish=device_finish,
   .device_get_gpu_timestamp=get_timestamp,.device_get_suspend_count=get_suspend_count,.device_check_status=check_status,
   .submitqueue_new=queue_new,.submitqueue_close=queue_close,
   .bo_init=tu_darwin_bo_init,.bo_init_dmabuf=tu_darwin_bo_init_dmabuf,.bo_init_host=tu_darwin_bo_init_host,
   .bo_export_dmabuf=tu_darwin_bo_export_dmabuf,.bo_map=tu_darwin_bo_map,.bo_allow_dump=tu_darwin_bo_allow_dump,
   .bo_finish=tu_darwin_bo_finish,.bo_set_metadata=tu_darwin_bo_set_metadata,.bo_get_metadata=tu_darwin_bo_get_metadata,
   .submit_create=tu_darwin_submit_create,.submit_finish=tu_darwin_submit_finish,
   .submit_add_entries=tu_darwin_submit_add_entries,.submit_add_bind=tu_darwin_submit_add_bind,
   .queue_submit=tu_darwin_queue_submit,.queue_wait_fence=tu_darwin_queue_wait_fence,
   .sparse_vma_init=sparse_init,.sparse_vma_finish=sparse_finish,
};

__attribute__((constructor)) static void configure_debug()
{
   char args[1100]{};size_t size=sizeof(args)-1;
   bool nobin=false,sync=false;
   if (!sysctlbyname("kern.bootargs",args,&size,nullptr,0)) {
      nobin=strstr(args,"pong_tu_nobin=1")!=nullptr;sync=strstr(args,"pong_tu_sync=1")!=nullptr;
   }
   if (nobin) {
      const char *base=getenv("TU_DEBUG");char value[256];
      snprintf(value,sizeof(value),"%s,nobin",base && *base?base:"sysmem,nocb");setenv("TU_DEBUG",value,1);
   }
   if (sync) setenv("TU_LIUQIN_SYNC","1",1);else unsetenv("TU_LIUQIN_SYNC");
   fprintf(stderr,"Pong Turnip63 backend=per-device TU_DEBUG=%s sync=%u\n",getenv("TU_DEBUG")?getenv("TU_DEBUG"):"default",sync);
}
VkResult tu_knl_darwin_load(tu_instance *instance)
{
   if (instance->vk.enabled_extensions.KHR_display) return vk_error(instance,VK_ERROR_INITIALIZATION_FAILED);
   tu_darwin_connection *probe=nullptr;
   int32_t status=tu_darwin_connection::create(tu_darwin_iokit_io(),&probe);
   if (status) return vk_startup_errorf(instance,status==kIOReturnNotFound?VK_ERROR_INCOMPATIBLE_DRIVER:VK_ERROR_INITIALIZATION_FAILED,
                                       "native GPU probe: 0x%x",uint32_t(status));
   // Copy facts, then release this probe only. Existing logical devices own
   // distinct ports and their own address spaces/fences throughout discovery.
   const auto info=probe->info();const auto params=probe->params();probe->release();
   auto *device=static_cast<tu_physical_device *>(vk_zalloc(&instance->vk.alloc,sizeof(tu_physical_device),8,VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE));
   if (!device) return vk_error(instance,VK_ERROR_OUT_OF_HOST_MEMORY);
   device->instance=instance;device->local_fd=-1;device->master_fd=-1;device->kgsl_dma_fd=-1;
   snprintf(device->fd_path,sizeof(device->fd_path),"NativeAdrenoGpu");
   device->dev_id.chip_id=info.chip_id;
   device->dev_id.gpu_id=((info.chip_id>>24)&255)*100+((info.chip_id>>16)&255)*10+((info.chip_id>>8)&255);
   if (info.chip_id==0x43050a01) device->dev_id.gpu_id=740;
   device->gmem_size=debug_get_num_option("TU_GMEM",info.gmem_bytes);
   device->gmem_base=info.gmem_base;device->uche_trap_base=info.uche_trap_base;
   device->has_set_iova=true;device->va_start=info.va_start;device->va_size=info.va_size;
   device->has_lazy_bos=false;device->has_sparse=false;device->has_sparse_prr=false;
   device->has_raytracing=false;device->has_preemption=false;device->is_perf_cntr_selectable=false;
   device->has_cached_coherent_memory=info.flags&NativeAdrenoDeviceCachedCoherent;
   device->has_host_import=params.flags&NativeAdrenoParamsHostImport;device->submitqueue_priority_count=1;
   device->ubwc_config.highest_bank_bit=info.highest_bank_bit;
   device->ubwc_config.bank_swizzle_levels=info.ubwc_swizzle;
   device->ubwc_config.macrotile_mode=info.macrotile_mode?FDL_MACROTILE_8_CHANNEL:FDL_MACROTILE_4_CHANNEL;
   device->timeline_type=vk_sync_timeline_get_type(&vk_darwin_sync_type);
   device->sync_types[0]=&vk_darwin_sync_type;device->sync_types[1]=&device->timeline_type.sync;device->sync_types[2]=nullptr;
   device->heap.size=MIN2(tu_get_system_heap_size(device),info.va_size);
   device->heap.used=0;device->heap.flags=VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
   instance->knl=&darwin_knl_funcs;
   VkResult result=tu_physical_device_init(device,instance);
   if (result!=VK_SUCCESS) {vk_free(&instance->vk.alloc,device);return result;}
   list_addtail(&device->vk.link,&instance->vk.physical_devices.list);return VK_SUCCESS;
}
