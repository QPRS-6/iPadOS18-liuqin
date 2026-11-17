/* SPDX-License-Identifier: MIT */
#pragma once
#include <IOKit/IOReturn.h>
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "NativeAdrenoABI.h"

/* The core has no Mesa dependency. Tests and IOKit use the same ownership,
 * wire validation, retirement and synchronization code through these ops. */
struct tu_darwin_port { uint32_t handle; bool tunnel; };
struct tu_darwin_io {
   void *cookie;
   int32_t (*open)(void *, tu_darwin_port *);
   int32_t (*close)(void *, uint32_t);
   int32_t (*call)(void *, uint32_t, uint32_t, const void *, size_t, void *, size_t *);
   int32_t (*map)(void *, uint32_t, uint32_t, void *, void **, uint64_t *);
   int32_t (*unmap)(void *, uint32_t, uint32_t, void *);
   uint64_t (*now_ns)(void *);
   void (*sleep_ns)(void *, uint64_t);
};

class tu_darwin_connection {
public:
   static int32_t create(const tu_darwin_io &, tu_darwin_connection **);
   void retain();
   void release();
   int32_t call(uint32_t, const void *, size_t, void *, size_t) const;
   int32_t map(uint32_t, void *, void **, uint64_t *) const;
   int32_t unmap(uint32_t, void *) const;
   uint64_t now() const { return io_.now_ns(io_.cookie); }
   void sleep(uint64_t ns) const { io_.sleep_ns(io_.cookie, ns); }
   uint64_t id() const { return id_; }
   const NativeAdrenoAbiParams &params() const { return params_; }
   const NativeAdrenoAbiDeviceInfo &info() const { return info_; }
   bool publish();
   void unpublish();
   /* An unqualified frontend cache-sync call may use exactly one live device.
    * The returned reference survives unpublish and device destruction. */
   static tu_darwin_connection *unique_live();
private:
   explicit tu_darwin_connection(const tu_darwin_io &io) : io_(io) {}
   ~tu_darwin_connection();
   tu_darwin_io io_;
   tu_darwin_port port_{};
   uint32_t selector_=0;
   uint64_t id_=0;
   std::atomic<unsigned> refs_{1};
   NativeAdrenoAbiParams params_{};
   NativeAdrenoAbiDeviceInfo info_{};
   tu_darwin_connection *next_=nullptr;
   bool published_=false;
};

enum tu_darwin_phase : unsigned {
   TD_OPEN, TD_WIRE, TD_ALLOC, TD_MAP, TD_CPU_MAP, TD_CPU_UNMAP,
   TD_SUBMIT, TD_WAIT, TD_RETIRE, TD_CAPACITY, TD_CLOSED, TD_PHASE_COUNT
};
void tu_darwin_report(uint64_t, tu_darwin_phase, int32_t, uint64_t=0, uint64_t=0);
uint64_t tu_darwin_deadline(uint64_t now, uint64_t relative);
bool tu_darwin_range_valid(uint64_t, uint64_t);

struct tu_darwin_bo {
   uint64_t owner=0, handle=0, iova=0, size=0;
   void *mapping=nullptr;
   uint32_t token=0;
   bool imported=false;
};
struct tu_darwin_sync {
   enum state_t { UNSIGNALED, SIGNALED, FENCE } state=UNSIGNALED;
   class tu_darwin_backend *owner=nullptr;
   uint32_t fence=0;
   uint64_t generation=0;
};

class tu_darwin_backend {
public:
   using release_range_fn = void (*)(void *, uint64_t, uint64_t);
   static int32_t create(tu_darwin_connection *, release_range_fn, void *, tu_darwin_backend **);
   ~tu_darwin_backend();
   void shutdown();
   tu_darwin_connection *connection() const { return conn_; }
   int32_t failure() const { return int32_t(uint32_t(failure_.load(std::memory_order_acquire))); }
   tu_darwin_phase failure_phase() const { return tu_darwin_phase(failure_.load(std::memory_order_acquire)>>32); }
   int32_t fail(int32_t, tu_darwin_phase);
   bool contains(uint64_t, uint64_t) const;
   /* Accepts ownership of a valid reserved IOVA. On failure its range is
    * returned only after native ownership is absent or BoFree succeeds. */
   int32_t new_bo(uint64_t, uint64_t, void *, bool, tu_darwin_bo *);
   int32_t map_bo(tu_darwin_bo *, void *placed);
   int32_t discard_bo(const tu_darwin_bo &); // never submitted
   int32_t retire_bo(const tu_darwin_bo &);  // captures this device's last fence
   int32_t collect();
   int32_t submit(const NativeAdrenoAbiSubmitBuffer *, unsigned, uint32_t *, uint64_t deadline);
   int32_t wait_fence(uint32_t, uint64_t deadline);
   uint32_t last_fence();
   unsigned batch_limit() const;
   int32_t check_status();
   void sync_init(tu_darwin_sync *, bool);
   int32_t sync_reset(tu_darwin_sync *);
   int32_t sync_move(tu_darwin_sync *, tu_darwin_sync *);
   int32_t sync_signal(tu_darwin_sync *, uint32_t);
   int32_t sync_wait(tu_darwin_sync *, bool pending, uint64_t deadline);
   int32_t sync_wait_many(tu_darwin_sync *const *, unsigned, bool any, bool pending, uint64_t deadline);
private:
   tu_darwin_backend(tu_darwin_connection *, release_range_fn, void *);
   int32_t cleanup_cpu(tu_darwin_bo *);
   int32_t enqueue(tu_darwin_bo, uint32_t);
   int32_t collect_locked();
   int32_t prune_fences_locked();
   int sync_cond_wait(uint64_t);
   tu_darwin_connection *conn_;
   release_range_fn release_range_;
   void *range_cookie_;
   std::atomic<uint64_t> failure_{0};
   pthread_mutex_t submit_lock_, retire_lock_, sync_lock_;
   pthread_cond_t sync_cond_;
   unsigned initialized_=0;
   bool published_=false;
   bool status_supported_=false;
   uint64_t initial_faults_=0;
   uint32_t fences_[8]{}, fence_count_=0, last_fence_=0;
   struct retired { tu_darwin_bo bo; uint32_t fence; } retired_[256]{};
   unsigned head_=0, count_=0;
};

const tu_darwin_io &tu_darwin_iokit_io();
