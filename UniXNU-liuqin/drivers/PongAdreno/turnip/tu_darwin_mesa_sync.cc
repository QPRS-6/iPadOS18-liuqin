/* SPDX-License-Identifier: MIT */
#include "tu_darwin_mesa.h"
#include <stdlib.h>

static tu_device *device_of(vk_device *vk) { return container_of(vk,tu_device,vk); }
static vk_darwin_sync *sync_of(vk_sync *vk) { return container_of(vk,vk_darwin_sync,vk); }
static VkResult wait_result(tu_device *dev, int32_t status)
{
   if (status==kIOReturnTimeout && !dev->darwin->failure()) return VK_TIMEOUT;
   return tu_darwin_error(dev,status,"sync wait",VK_ERROR_DEVICE_LOST);
}
static VkResult sync_init(vk_device *vk, vk_sync *sync, uint64_t value)
{
   auto *dev=device_of(vk);dev->darwin->sync_init(&sync_of(sync)->state,value!=0);
   return tu_darwin_error(dev,dev->darwin->failure(),"sync init",VK_ERROR_DEVICE_LOST);
}
static void sync_finish(vk_device *vk, vk_sync *sync) { (void)vk;(void)sync; }
static VkResult sync_reset(vk_device *vk, vk_sync *sync)
{
   auto *dev=device_of(vk);return tu_darwin_error(dev,dev->darwin->sync_reset(&sync_of(sync)->state),"sync reset",VK_ERROR_DEVICE_LOST);
}
static VkResult sync_move(vk_device *vk, vk_sync *dst, vk_sync *src)
{
   auto *dev=device_of(vk);return tu_darwin_error(dev,dev->darwin->sync_move(&sync_of(dst)->state,&sync_of(src)->state),"sync move",VK_ERROR_DEVICE_LOST);
}
static VkResult sync_signal(vk_device *vk, vk_sync *sync, uint64_t value)
{
   (void)value;auto *dev=device_of(vk);
   return tu_darwin_error(dev,dev->darwin->sync_signal(&sync_of(sync)->state,0),"CPU sync signal",VK_ERROR_DEVICE_LOST);
}
static VkResult sync_wait(vk_device *vk, vk_sync *sync, uint64_t value, vk_sync_wait_flags flags, uint64_t deadline)
{
   (void)value;auto *dev=device_of(vk);
   return wait_result(dev,dev->darwin->sync_wait(&sync_of(sync)->state,flags&VK_SYNC_WAIT_PENDING,deadline));
}
VkResult tu_darwin_wait_syncs(tu_device *dev, const struct vk_sync_wait *waits, uint32_t count,
                              bool any, bool pending, uint64_t deadline)
{
   if (!count) return VK_SUCCESS;
   tu_darwin_sync *local[16];tu_darwin_sync **states=local;
   if (count>16) {
      if (size_t(count)>SIZE_MAX/sizeof(*states)) return VK_ERROR_OUT_OF_HOST_MEMORY;
      states=static_cast<tu_darwin_sync **>(malloc(size_t(count)*sizeof(*states)));
      if (!states) return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   for (uint32_t i=0;i<count;i++) states[i]=&sync_of(waits[i].sync)->state;
   const int32_t status=dev->darwin->sync_wait_many(states,count,any,pending,deadline);
   if (states!=local) free(states);
   return wait_result(dev,status);
}
static VkResult sync_wait_many(vk_device *vk, uint32_t count, const struct vk_sync_wait *waits,
                                vk_sync_wait_flags flags, uint64_t deadline)
{
   return tu_darwin_wait_syncs(device_of(vk),waits,count,flags&VK_SYNC_WAIT_ANY,flags&VK_SYNC_WAIT_PENDING,deadline);
}
VkResult tu_darwin_publish_signals(tu_device *dev, const struct vk_sync_signal *signals, uint32_t count, uint32_t fence)
{
   for (uint32_t i=0;i<count;i++) {
      int32_t status=dev->darwin->sync_signal(&sync_of(signals[i].sync)->state,fence);
      if (status) return tu_darwin_error(dev,status,"submit signal",VK_ERROR_DEVICE_LOST);
   }
   return VK_SUCCESS;
}
const vk_sync_type vk_darwin_sync_type={
   .size=sizeof(vk_darwin_sync),
   .features=vk_sync_features(VK_SYNC_FEATURE_BINARY|VK_SYNC_FEATURE_GPU_WAIT|VK_SYNC_FEATURE_GPU_MULTI_WAIT|
      VK_SYNC_FEATURE_CPU_WAIT|VK_SYNC_FEATURE_CPU_RESET|VK_SYNC_FEATURE_CPU_SIGNAL|VK_SYNC_FEATURE_WAIT_ANY|VK_SYNC_FEATURE_WAIT_PENDING),
   .init=sync_init,.finish=sync_finish,.signal=sync_signal,.reset=sync_reset,.move=sync_move,
   .wait=sync_wait,.wait_many=sync_wait_many,
};
