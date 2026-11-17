// SPDX-License-Identifier: MIT
#define main core_fixture_main
#include "turnip-backend-core.cpp"
#undef main
#include "tu_darwin_mesa.h"
#include "adapter-error.inc"
#include "tu_darwin_memory.cc"
#include "tu_darwin_submit.cc"

struct MesaDevice {
   Driver driver;tu_device dev;
   static void released(void *opaque,uint64_t start,uint64_t bytes) {
      auto *self=static_cast<MesaDevice *>(opaque);Driver::release_range(&self->driver,start,bytes);
      std::lock_guard<std::mutex> lock(self->dev.vma_mutex);self->dev.vma.free_size+=bytes;
   }
   MesaDevice() {
      pthread_mutex_init(&dev.submit_mutex,nullptr);
      tu_darwin_connection *c=nullptr;check(!tu_darwin_connection::create(driver.io(),&c));
      check(!tu_darwin_backend::create(c,released,this,&dev.darwin));dev.va_size=c->info().va_size;
      dev.vma.next=c->info().va_start;dev.vma.free_size=dev.va_size;c->release();
   }
   ~MesaDevice(){delete dev.darwin;pthread_mutex_destroy(&dev.submit_mutex);}
   tu_bo *bo() {
      tu_bo *out=nullptr;
      check(tu_darwin_bo_init(&dev,nullptr,&out,8192,0,0,TU_BO_ALLOC_NO_FLAGS,nullptr,"test")==VK_SUCCESS);
      check(out && out->darwin_owner==dev.darwin->connection()->id());return out;
   }
};
VkResult tu_darwin_wait_syncs(tu_device *dev,const struct vk_sync_wait *waits,uint32_t count,bool any,bool pending,uint64_t deadline)
{
   std::vector<tu_darwin_sync *> states;
   for(unsigned i=0;i<count;i++)states.push_back(&container_of(waits[i].sync,vk_darwin_sync,vk)->state);
   auto status=dev->darwin->sync_wait_many(states.data(),count,any,pending,deadline);
   if(status==kIOReturnTimeout && !dev->darwin->failure())return VK_TIMEOUT;
   return status?VK_ERROR_DEVICE_LOST:VK_SUCCESS;
}
VkResult tu_darwin_publish_signals(tu_device *dev,const struct vk_sync_signal *signals,uint32_t count,uint32_t fence)
{
   for(unsigned i=0;i<count;i++)if(dev->darwin->sync_signal(&container_of(signals[i].sync,vk_darwin_sync,vk)->state,fence))return VK_ERROR_DEVICE_LOST;
   return VK_SUCCESS;
}
int main() {
   unsetenv("TU_LIUQIN_IBDUMP");
   {
      MesaDevice m;auto *bo=m.bo();m.driver.short_mapping=true;m.driver.unmap_error=kIOReturnIOError;
      check(tu_darwin_bo_map(&m.dev,bo,nullptr)==VK_ERROR_DEVICE_LOST);
      check(!bo->map && bo->darwin_failed_map && bo->darwin_token);
      tu_darwin_bo_finish(&m.dev,bo);check(!bo->gem_handle && m.driver.released.empty());
   }
   {
      MesaDevice m;m.dev.fail_lookup=true;tu_bo *bo=reinterpret_cast<tu_bo *>(1);
      check(tu_darwin_bo_init(&m.dev,nullptr,&bo,8192,0,0,TU_BO_ALLOC_NO_FLAGS,nullptr,"test")==VK_ERROR_OUT_OF_HOST_MEMORY);
      check(!bo && m.driver.released.size()==1 && m.driver.free_calls==1);
   }
   {
      MesaDevice m;auto *bo=m.bo();tu_cs_entry entry{bo,0,4};
      auto *submission=tu_darwin_submit_create(&m.dev);check(submission);
      tu_darwin_submit_add_entries(&m.dev,submission,&entry,1);
      vk_darwin_sync ready{};m.dev.darwin->sync_init(&ready.state,false);
      vk_sync_wait wait{&ready.vk};tu_queue a{&m.dev,0};std::promise<void> started;
      auto blocked=std::async(std::launch::async,[&] {
         // Match vk_queue's pending dependency gate before driver_submit.
         started.set_value();check(tu_darwin_wait_syncs(&m.dev,&wait,1,false,true,UINT64_MAX)==VK_SUCCESS);
         pthread_mutex_lock(&m.dev.submit_mutex);
         auto result=tu_darwin_queue_submit(&a,submission,&wait,1,nullptr,0,nullptr);
         pthread_mutex_unlock(&m.dev.submit_mutex);return result;
      });
      started.get_future().wait();
      // Pending waits outside the driver callback let queue B publish;
      // the KMD itself must preserve Mesa's preparation lock.
      pthread_mutex_lock(&m.dev.submit_mutex);
      tu_queue b{&m.dev,0};vk_sync_signal signal{&ready.vk};auto *empty=tu_darwin_submit_create(&m.dev);
      check(tu_darwin_queue_submit(&b,empty,nullptr,0,&signal,1,nullptr)==VK_SUCCESS);
      pthread_mutex_unlock(&m.dev.submit_mutex);
      check(blocked.get()==VK_SUCCESS && a.fence);
      tu_darwin_submit_finish(&m.dev,empty);tu_darwin_submit_finish(&m.dev,submission);
      m.driver.complete(uint32_t(a.fence));tu_darwin_bo_finish(&m.dev,bo);check(m.driver.released.size()==1);
   }
   {
      MesaDevice m;tu_queue q{&m.dev,0};auto *empty=tu_darwin_submit_create(&m.dev);
      vk_darwin_sync sync{};m.dev.darwin->sync_init(&sync.state,false);vk_sync_wait wait{&sync.vk};
      pthread_mutex_lock(&m.dev.submit_mutex);
      check(tu_darwin_queue_submit(&q,empty,&wait,1,nullptr,0,nullptr)==VK_ERROR_DEVICE_LOST);
      check(pthread_mutex_trylock(&m.dev.submit_mutex)!=0);
      pthread_mutex_unlock(&m.dev.submit_mutex);check(!m.driver.submits);
      tu_darwin_submit_finish(&m.dev,empty);
   }
   {
      MesaDevice m;auto *bo=m.bo();tu_cs_entry entry{bo,0,4};auto *submission=tu_darwin_submit_create(&m.dev);
      for(unsigned i=0;i<129;i++)tu_darwin_submit_add_entries(&m.dev,submission,&entry,1);
      tu_queue q{&m.dev,0};pthread_mutex_lock(&m.dev.submit_mutex);
      check(tu_darwin_queue_submit(&q,submission,nullptr,0,nullptr,0,nullptr)==VK_ERROR_DEVICE_LOST);
      pthread_mutex_unlock(&m.dev.submit_mutex);check(!m.driver.submits && !q.fence);tu_darwin_submit_finish(&m.dev,submission);
   }
   {
      MesaDevice m;auto *bo=m.bo();m.driver.next_fence=0x80000001;
      auto *submission=tu_darwin_submit_create(&m.dev);tu_cs_entry entry{bo,0,4};tu_darwin_submit_add_entries(&m.dev,submission,&entry,1);
      tu_queue q{&m.dev,0};pthread_mutex_lock(&m.dev.submit_mutex);
      check(tu_darwin_queue_submit(&q,submission,nullptr,0,nullptr,0,nullptr)==VK_SUCCESS);pthread_mutex_unlock(&m.dev.submit_mutex);
      auto *empty=tu_darwin_submit_create(&m.dev);vk_darwin_sync sync{};m.dev.darwin->sync_init(&sync.state,false);vk_sync_signal signal{&sync.vk};
      pthread_mutex_lock(&m.dev.submit_mutex);check(tu_darwin_queue_submit(&q,empty,nullptr,0,&signal,1,nullptr)==VK_SUCCESS);pthread_mutex_unlock(&m.dev.submit_mutex);
      check(m.dev.darwin->sync_wait(&sync.state,false,0)==kIOReturnTimeout);
      m.driver.complete(0x80000001);check(!m.dev.darwin->sync_wait(&sync.state,false,0));
      tu_darwin_submit_finish(&m.dev,empty);tu_darwin_submit_finish(&m.dev,submission);tu_darwin_bo_finish(&m.dev,bo);
   }
   std::puts("PASS actual Mesa adapters: failed-map publication, metadata OOM rollback, pending dependency gate and caller-lock preservation, atomic batch refusal and high-bit empty-submit fence");
}
