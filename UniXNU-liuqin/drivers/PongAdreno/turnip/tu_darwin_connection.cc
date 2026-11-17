/* SPDX-License-Identifier: MIT */
#include "tu_darwin_core.h"
#include "tu_darwin_host_sync.h"
#include <new>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t live_lock=PTHREAD_MUTEX_INITIALIZER;
static tu_darwin_connection *live_head;
static std::atomic<uint64_t> next_id{1};
static constexpr uint32_t tunnel_magic=0x4e544e4c;

bool tu_darwin_range_valid(uint64_t address, uint64_t bytes)
{
   return address && bytes && !((address|bytes)&4095) && bytes<=UINT64_MAX-address;
}
uint64_t tu_darwin_deadline(uint64_t now, uint64_t relative)
{
   return relative>UINT64_MAX-now ? UINT64_MAX : now+relative;
}
void tu_darwin_report(uint64_t id, tu_darwin_phase phase, int32_t status, uint64_t a, uint64_t b)
{
   static std::atomic<unsigned> counts[TD_PHASE_COUNT];
   if (phase>=TD_PHASE_COUNT || counts[phase].fetch_add(1,std::memory_order_relaxed)>=16) return;
   static const char *const names[]={"open","wire","allocate","map","cpu-map","cpu-unmap",
      "submit","wait","retire","capacity","closed"};
   fprintf(stderr,"Pong Turnip63: device=%llu stage=%s status=0x%x a=%llu b=%llu\n",
      (unsigned long long)id,names[phase],uint32_t(status),(unsigned long long)a,(unsigned long long)b);
}

int32_t tu_darwin_connection::create(const tu_darwin_io &io, tu_darwin_connection **out)
{
   if (!out) return kIOReturnBadArgument;
   *out=nullptr;
   if (!io.open || !io.close || !io.call || !io.map || !io.unmap || !io.now_ns || !io.sleep_ns)
      return kIOReturnBadArgument;
   auto *c=new(std::nothrow) tu_darwin_connection(io);
   if (!c) return kIOReturnNoMemory;
   c->id_=next_id.fetch_add(1,std::memory_order_relaxed);
   int32_t status=io.open(io.cookie,&c->port_);
   if (!status && !c->port_.handle) status=kIOReturnNotReady;
   if (!status && c->port_.tunnel) {
      const uint32_t probe[]={tunnel_magic,0xffffffffu};
      status=kIOReturnUnsupported;
      for (unsigned selector=0;selector<256;selector++) {
         if (!io.call(io.cookie,c->port_.handle,selector,probe,sizeof(probe),nullptr,nullptr)) {
            c->selector_=selector;status=0;break;
         }
      }
   }
   if (!status) status=c->call(NativeAdrenoMethodQuery,nullptr,0,&c->params_,sizeof(c->params_));
   if (!status && (c->params_.abi_version!=NativeAdrenoAbiVersion || c->params_.input_bits<32 ||
       c->params_.input_bits>48 || c->params_.output_bits<32 || c->params_.output_bits>48 ||
       !c->params_.max_bo_bytes || (c->params_.max_bo_bytes&4095))) status=kIOReturnUnsupported;
   if (!status) status=c->call(NativeAdrenoMethodGetDeviceInfo,nullptr,0,&c->info_,sizeof(c->info_));
   if (!status && (!c->info_.chip_id || !c->info_.gmem_bytes ||
       !(c->info_.flags&NativeAdrenoDeviceClientIova) ||
       !tu_darwin_range_valid(c->info_.va_start,c->info_.va_size) ||
       c->info_.va_start+c->info_.va_size>(1ULL<<c->params_.input_bits))) status=kIOReturnBadArgument;
   if (status) {tu_darwin_report(c->id_,TD_OPEN,status);c->release();return status;}
   *out=c;
   return 0;
}
tu_darwin_connection::~tu_darwin_connection()
{
   unpublish();
   if (port_.handle) {
      const int32_t status=io_.close(io_.cookie,port_.handle);
      if (status) tu_darwin_report(id_,TD_CLOSED,status);
   }
}
void tu_darwin_connection::retain() { refs_.fetch_add(1,std::memory_order_relaxed); }
void tu_darwin_connection::release()
{
   if (refs_.fetch_sub(1,std::memory_order_acq_rel)==1) delete this;
}
int32_t tu_darwin_connection::call(uint32_t method, const void *input, size_t input_size,
                                  void *output, size_t output_size) const
{
   if (!port_.handle) return kIOReturnNotReady;
   if ((input_size && !input) || (output_size && !output) || input_size>sizeof(NativeAdrenoAbiSubmitBatch))
      return kIOReturnBadArgument;
   uint8_t wire[8+sizeof(NativeAdrenoAbiSubmitBatch)];
   if (output_size) memset(output,0,output_size);
   size_t received=output_size;
   uint32_t selector=method;
   if (port_.tunnel) {
      const uint32_t header[]={tunnel_magic,method};
      memcpy(wire,header,sizeof(header));
      if (input_size) memcpy(wire+sizeof(header),input,input_size);
      input=wire;input_size+=sizeof(header);selector=selector_;
   }
   int32_t status=io_.call(io_.cookie,port_.handle,selector,input,input_size,
                         output_size?output:nullptr,output_size?&received:nullptr);
   if (!status && received!=output_size) {
      tu_darwin_report(id_,TD_WIRE,kIOReturnUnderrun,method,received);
      return kIOReturnUnderrun;
   }
   return status;
}
int32_t tu_darwin_connection::map(uint32_t token, void *placed, void **address, uint64_t *bytes) const
{
   *address=nullptr;*bytes=0;
   return io_.map(io_.cookie,port_.handle,token,placed,address,bytes);
}
int32_t tu_darwin_connection::unmap(uint32_t token, void *address) const
{
   return io_.unmap(io_.cookie,port_.handle,token,address);
}
bool tu_darwin_connection::publish()
{
   pthread_mutex_lock(&live_lock);
   bool added=!published_;
   if (added) {next_=live_head;live_head=this;published_=true;}
   pthread_mutex_unlock(&live_lock);
   return added;
}
void tu_darwin_connection::unpublish()
{
   pthread_mutex_lock(&live_lock);
   if (published_) {
      auto **at=&live_head;
      while (*at && *at!=this) at=&(*at)->next_;
      if (*at) *at=next_;
      next_=nullptr;published_=false;
   }
   pthread_mutex_unlock(&live_lock);
}
tu_darwin_connection *tu_darwin_connection::unique_live()
{
   pthread_mutex_lock(&live_lock);
   auto *c=live_head && !live_head->next_ ? live_head : nullptr;
   if (c) c->retain();
   pthread_mutex_unlock(&live_lock);
   return c;
}

extern "C" __attribute__((visibility("default"))) int32_t
tu_darwin_sync_host_range(uint64_t address, uint64_t bytes, uint32_t phase)
{
   if (!tu_darwin_range_valid(address,bytes) || phase>2) return kIOReturnBadArgument;
   auto *c=tu_darwin_connection::unique_live();
   if (!c) return kIOReturnNotFound;
   NativeAdrenoAbiSyncHostRange request{address,bytes,phase,0};
   int32_t status=kIOReturnUnsupported;
   if ((c->params().flags&NativeAdrenoParamsExplicitHostSync) &&
       (phase!=2 || (c->params().flags&NativeAdrenoParamsExplicitHostAcquire)))
      status=c->call(NativeAdrenoMethodSyncHostRange,&request,sizeof(request),nullptr,0);
   c->release();return status;
}
extern "C" __attribute__((visibility("default"))) int32_t
tu_darwin_sync_host_batch(const uint64_t *ranges, uint32_t count, uint32_t phase, uint64_t *covered)
{
   if (!covered) return kIOReturnBadArgument;
   *covered=0;
   if (!ranges || !count || count>64 || phase>2) return kIOReturnBadArgument;
   NativeAdrenoAbiSyncHostBatch request{};request.count=count;request.after=phase;
   for (unsigned i=0;i<count;i++) {
      if (!tu_darwin_range_valid(ranges[2*i],ranges[2*i+1])) return kIOReturnBadArgument;
      request.ranges[i]={ranges[2*i],ranges[2*i+1]};
   }
   auto *c=tu_darwin_connection::unique_live();
   if (!c) return 0; // no unambiguous owner: zero coverage preserves frontend maintenance
   NativeAdrenoAbiSyncHostBatchReply reply{};
   int32_t status=kIOReturnUnsupported;
   if ((c->params().flags&NativeAdrenoParamsHostSyncBatch) &&
       (phase!=2 || (c->params().flags&NativeAdrenoParamsExplicitHostAcquire)))
      status=c->call(NativeAdrenoMethodSyncHostBatch,&request,sizeof(request),&reply,sizeof(reply));
   if (!status) *covered=reply.covered & (count==64 ? UINT64_MAX : (1ULL<<count)-1);
   c->release();return status;
}
