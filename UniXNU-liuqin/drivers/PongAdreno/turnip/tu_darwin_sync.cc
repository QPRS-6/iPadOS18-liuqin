/* SPDX-License-Identifier: MIT */
#include "tu_darwin_core.h"
#include <errno.h>
#include <time.h>

/* One state lock per device, independent of Mesa's submission mutex. No GPU
 * wait holds this lock. Reset/move/publication and pending waits use it alike. */
void tu_darwin_backend::sync_init(tu_darwin_sync *sync, bool signaled)
{
   pthread_mutex_lock(&sync_lock_);
   sync->owner=this;sync->state=signaled?tu_darwin_sync::SIGNALED:tu_darwin_sync::UNSIGNALED;
   sync->fence=0;sync->generation=1;
   pthread_mutex_unlock(&sync_lock_);
}
int32_t tu_darwin_backend::sync_reset(tu_darwin_sync *sync)
{
   if (!sync || sync->owner!=this) return kIOReturnBadArgument;
   pthread_mutex_lock(&sync_lock_);
   sync->state=tu_darwin_sync::UNSIGNALED;sync->fence=0;sync->generation++;
   pthread_cond_broadcast(&sync_cond_);
   pthread_mutex_unlock(&sync_lock_);
   return failure();
}
int32_t tu_darwin_backend::sync_move(tu_darwin_sync *dst, tu_darwin_sync *src)
{
   if (!dst || !src || dst->owner!=this || src->owner!=this) return kIOReturnBadArgument;
   if (dst==src) return failure();
   pthread_mutex_lock(&sync_lock_);
   dst->state=src->state;dst->fence=src->fence;dst->generation++;
   src->state=tu_darwin_sync::UNSIGNALED;src->fence=0;src->generation++;
   pthread_cond_broadcast(&sync_cond_);
   pthread_mutex_unlock(&sync_lock_);
   return failure();
}
int32_t tu_darwin_backend::sync_signal(tu_darwin_sync *sync, uint32_t fence)
{
   if (!sync || sync->owner!=this) return kIOReturnBadArgument;
   pthread_mutex_lock(&sync_lock_);
   int32_t status=failure();
   if (!status) {
      sync->state=fence?tu_darwin_sync::FENCE:tu_darwin_sync::SIGNALED;
      sync->fence=fence;sync->generation++;
      pthread_cond_broadcast(&sync_cond_);
   }
   pthread_mutex_unlock(&sync_lock_);return status;
}
int tu_darwin_backend::sync_cond_wait(uint64_t deadline)
{
   if (deadline==UINT64_MAX) return pthread_cond_wait(&sync_cond_,&sync_lock_);
   uint64_t now=conn_->now();
   if (deadline<=now) return ETIMEDOUT;
   uint64_t remaining=deadline-now;
   struct timespec relative{time_t(remaining/1000000000ULL),long(remaining%1000000000ULL)};
#if defined(__APPLE__)
   // Darwin's default timed condition wait uses wall time; GPU deadlines do not.
   return pthread_cond_timedwait_relative_np(&sync_cond_,&sync_lock_,&relative);
#else
   struct timespec absolute{};clock_gettime(CLOCK_REALTIME,&absolute);
   absolute.tv_sec+=relative.tv_sec;
   absolute.tv_nsec+=relative.tv_nsec;
   if (absolute.tv_nsec>=1000000000L) {absolute.tv_nsec-=1000000000L;absolute.tv_sec++;}
   return pthread_cond_timedwait(&sync_cond_,&sync_lock_,&absolute);
#endif
}
int32_t tu_darwin_backend::sync_wait(tu_darwin_sync *sync, bool pending, uint64_t deadline)
{
   if (!sync || sync->owner!=this) return kIOReturnBadArgument;
   for (;;) {
      pthread_mutex_lock(&sync_lock_);
      while (!failure() && sync->state==tu_darwin_sync::UNSIGNALED) {
         int result=sync_cond_wait(deadline);
         if (result) {
            pthread_mutex_unlock(&sync_lock_);
            return result==ETIMEDOUT ? int32_t(kIOReturnTimeout) : fail(kIOReturnIOError,TD_WAIT);
         }
      }
      int32_t status=failure();
      if (status || pending || sync->state==tu_darwin_sync::SIGNALED) {
         pthread_mutex_unlock(&sync_lock_);return status;
      }
      const uint32_t fence=sync->fence;
      const uint64_t generation=sync->generation;
      pthread_mutex_unlock(&sync_lock_);
      status=wait_fence(fence,deadline);
      if (status) return status;
      pthread_mutex_lock(&sync_lock_);
      const bool same=sync->generation==generation && sync->state==tu_darwin_sync::FENCE && sync->fence==fence;
      if (same) {sync->state=tu_darwin_sync::SIGNALED;sync->generation++;pthread_cond_broadcast(&sync_cond_);}
      pthread_mutex_unlock(&sync_lock_);
      if (same) return collect();
   }
}
int32_t tu_darwin_backend::sync_wait_many(tu_darwin_sync *const *syncs, unsigned count,
                                         bool any, bool pending, uint64_t deadline)
{
   if (!count) return 0;
   if (!syncs) return kIOReturnBadArgument;
   for (unsigned i=0;i<count;i++) if (!syncs[i] || syncs[i]->owner!=this) return kIOReturnBadArgument;
   if (!any) {
      for (unsigned i=0;i<count;i++) {
         int32_t status=sync_wait(syncs[i],pending,deadline);
         if (status) return status;
      }
      return 0;
   }
   for (;;) {
      for (unsigned i=0;i<count;i++) {
         int32_t status=sync_wait(syncs[i],pending,0);
         if (!status) return 0;
         if (status!=kIOReturnTimeout) return status; // never turn device loss into an endless wait
      }
      uint64_t now=conn_->now();
      if (deadline!=UINT64_MAX && now>=deadline) return kIOReturnTimeout;
      uint64_t pause=1000000ULL;
      if (deadline!=UINT64_MAX && deadline-now<pause) pause=deadline-now;
      conn_->sleep(pause);
   }
}
