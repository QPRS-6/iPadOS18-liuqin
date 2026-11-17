// SPDX-License-Identifier: MIT
#include "tu_darwin_core.h"
#include "tu_darwin_host_sync.h"
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#define check(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::_Exit(1);}}while(0)
struct Driver {
   struct Bo {uint64_t size=0,iova=0,host=0;unsigned token=0,leases=0;void *mapping=nullptr;};
   struct Port {uint64_t base=0,next_handle=1;std::map<uint64_t,Bo> bos;std::set<uint32_t> submitted,done;};
   std::mutex mutex;std::condition_variable condition;
   std::map<uint32_t,Port> ports;
   std::vector<uint32_t> closed;
   std::vector<std::pair<uint64_t,uint64_t>> released;
   uint32_t next_port=1,next_fence=1,flags=63,short_reply=0,input_bits=32;
   uint64_t time=1000000000ULL,busy_until=0,base_override=0,window_bytes=128ULL<<20;
   int32_t open_error=0,alloc_error=0,map_error=0,cpu_map_error=0,unmap_error=0,lease_error=0,free_error=0,wait_error=0;
   bool tunnel=false,short_mapping=false,wrong_placement=false,free_busy=false,auto_complete=false;
   bool block_sync=false,sync_inside=false,release_sync=false;
   bool block_clock=false,clock_inside=false,release_clock=false;
   unsigned calls=0,submits=0,free_calls=0,cpu_unmaps=0,native_unmaps=0,sync_calls=0;
   uint32_t last_phase=99;
   static Driver &of(void *cookie){return *static_cast<Driver *>(cookie);}
   template<class T> static T input(const void *bytes,size_t size) {
      check(bytes && size==sizeof(T));T value;std::memcpy(&value,bytes,size);return value;
   }
   template<class T> int32_t output(uint32_t method,const T &value,void *bytes,size_t *size) {
      check(bytes && size && *size>=sizeof(T));std::memcpy(bytes,&value,sizeof(T));
      *size=sizeof(T)-(short_reply==method?1:0);return 0;
   }
   static int32_t open(void *cookie,tu_darwin_port *out) {
      auto &d=of(cookie);std::lock_guard<std::mutex> lock(d.mutex);
      *out={};if(d.open_error)return d.open_error;
      uint32_t p=d.next_port++;d.ports[p].base=d.base_override?d.base_override:0x20000000ULL+uint64_t(p%20)*0x08000000ULL;
      *out={p,d.tunnel};return 0;
   }
   static int32_t close(void *cookie,uint32_t port) {
      auto &d=of(cookie);std::lock_guard<std::mutex> lock(d.mutex);
      check(d.ports.count(port)==1);d.ports.erase(port);d.closed.push_back(port);return 0;
   }
   static int32_t call(void *cookie,uint32_t port,uint32_t method,const void *bytes,size_t size,void *out,size_t *received) {
      auto &d=of(cookie);std::unique_lock<std::mutex> lock(d.mutex);
      check(d.ports.count(port)==1);d.calls++;
      if(d.tunnel) {
         if(method!=7)return kIOReturnUnsupported;
         check(size>=8);uint32_t header[2];std::memcpy(header,bytes,8);check(header[0]==0x4e544e4c);
         if(header[1]==0xffffffff)return 0;
         method=header[1];bytes=static_cast<const char *>(bytes)+8;size-=8;
      }
      auto &p=d.ports.at(port);
      switch(method) {
      case NativeAdrenoMethodQuery: {
         NativeAdrenoAbiParams value{1,d.input_bits,40,d.flags,1ULL<<28};return d.output(method,value,out,received);
      }
      case NativeAdrenoMethodGetDeviceInfo: {
         NativeAdrenoAbiDeviceInfo value{};value.chip_id=0x07030002;value.gmem_bytes=2<<20;
         value.va_start=p.base;value.va_size=d.window_bytes;value.flags=NativeAdrenoDeviceClientIova;
         return d.output(method,value,out,received);
      }
      case NativeAdrenoMethodGetStatus: {
         NativeAdrenoAbiStatus value{};return d.output(method,value,out,received);
      }
      case NativeAdrenoMethodBoAlloc:
      case NativeAdrenoMethodBoImport: {
         if(d.alloc_error)return d.alloc_error;
         uint64_t length=0,host=0;
         if(method==NativeAdrenoMethodBoAlloc)length=input<NativeAdrenoAbiBoAlloc>(bytes,size).bytes;
         else {auto r=input<NativeAdrenoAbiBoImport>(bytes,size);length=r.bytes;host=r.address;}
         auto handle=p.next_handle++;p.bos[handle]={length,0,host,0,0,nullptr};
         NativeAdrenoAbiBoAllocReply value{handle};return d.output(method,value,out,received);
      }
      case NativeAdrenoMethodBoMap: {
         auto r=input<NativeAdrenoAbiBoMap>(bytes,size);check(p.bos.count(r.handle));
         auto &bo=p.bos.at(r.handle);check(r.iova>=p.base && r.iova+bo.size<=p.base+d.window_bytes);
         check(!r.writeback && r.shareability==2);bo.iova=r.iova;
         if(d.map_error)return d.map_error; // simulates a partially modified map too
         NativeAdrenoAbiBoMapReply value{r.iova};return d.output(method,value,out,received);
      }
      case NativeAdrenoMethodBoCpuMap: {
         auto r=input<NativeAdrenoAbiBoCpuMap>(bytes,size);check(p.bos.count(r.handle));
         auto &bo=p.bos.at(r.handle);bo.token=uint32_t(r.handle);bo.leases++;
         NativeAdrenoAbiBoCpuMapReply value{bo.token,0,bo.size};return d.output(method,value,out,received);
      }
      case NativeAdrenoMethodBoCpuUnmap: {
         auto r=input<NativeAdrenoAbiBoCpuUnmap>(bytes,size);check(p.bos.count(r.handle));
         auto &bo=p.bos.at(r.handle);check(bo.leases && bo.token==r.token && !bo.mapping);
         d.native_unmaps++;if(d.lease_error)return d.lease_error;bo.leases--;return 0;
      }
      case NativeAdrenoMethodBoFree: {
         auto r=input<NativeAdrenoAbiBoFree>(bytes,size);check(p.bos.count(r.handle));d.free_calls++;
         auto &bo=p.bos.at(r.handle);check(!bo.leases && !bo.mapping);
         if(d.free_error)return d.free_error;
         if(d.free_busy)return kIOReturnBusy;
         p.bos.erase(r.handle);return 0;
      }
      case NativeAdrenoMethodSubmit:
      case NativeAdrenoMethodSubmitBatch: {
         check(size==(method==NativeAdrenoMethodSubmit?sizeof(NativeAdrenoAbiSubmit):sizeof(NativeAdrenoAbiSubmitBatch)));
         NativeAdrenoAbiSubmitBatch r{};std::memcpy(&r,bytes,size);
         check(r.buffer_count && r.buffer_count<=128);
         for(unsigned i=0;i<r.buffer_count;i++)check(p.bos.count(r.buffers[i].handle));
         if(d.time<d.busy_until)return kIOReturnBusy;
         uint32_t fence=d.next_fence++;p.submitted.insert(fence);d.submits++;
         NativeAdrenoAbiSubmitReply value{fence};return d.output(method,value,out,received);
      }
      case NativeAdrenoMethodWaitFence: {
         auto r=input<NativeAdrenoAbiWaitFence>(bytes,size);check(p.submitted.count(uint32_t(r.fence)));
         if(d.wait_error)return d.wait_error;
         if(p.done.count(uint32_t(r.fence)))return 0;
         if(r.timeout_ns && d.auto_complete) {p.done.insert(p.submitted.begin(),p.submitted.end());return 0;}
         d.time+=r.timeout_ns;return kIOReturnTimeout;
      }
      case NativeAdrenoMethodSyncHostRange: {
         auto r=input<NativeAdrenoAbiSyncHostRange>(bytes,size);d.sync_calls++;d.last_phase=r.after;return 0;
      }
      case NativeAdrenoMethodSyncHostBatch: {
         auto r=input<NativeAdrenoAbiSyncHostBatch>(bytes,size);d.sync_calls++;d.last_phase=r.after;
         if(d.block_sync) {d.sync_inside=true;d.condition.notify_all();d.condition.wait(lock,[&]{return d.release_sync;});}
         NativeAdrenoAbiSyncHostBatchReply value{UINT64_MAX,0,0};return d.output(method,value,out,received);
      }
      default:return kIOReturnUnsupported;
      }
   }
   static int32_t map(void *cookie,uint32_t port,uint32_t token,void *placed,void **address,uint64_t *bytes) {
      auto &d=of(cookie);std::lock_guard<std::mutex> lock(d.mutex);check(d.ports.count(port));
      auto &bo=d.ports.at(port).bos.at(token);*address=nullptr;*bytes=0;
      if(d.cpu_map_error)return d.cpu_map_error;
      *address=placed && !d.wrong_placement ? placed : reinterpret_cast<void *>(0x10000000ULL+uint64_t(port)*0x100000+uint64_t(token)*4096);
      *bytes=d.short_mapping?bo.size-4096:bo.size;bo.mapping=*address;return 0;
   }
   static int32_t unmap(void *cookie,uint32_t port,uint32_t token,void *address) {
      auto &d=of(cookie);std::lock_guard<std::mutex> lock(d.mutex);auto &bo=d.ports.at(port).bos.at(token);
      check(bo.mapping==address);d.cpu_unmaps++;if(d.unmap_error)return d.unmap_error;bo.mapping=nullptr;return 0;
   }
   static uint64_t now(void *cookie) {
      auto &d=of(cookie);std::unique_lock<std::mutex> lock(d.mutex);
      if(d.block_clock) {d.clock_inside=true;d.condition.notify_all();d.condition.wait(lock,[&]{return d.release_clock;});d.block_clock=false;}
      return d.time;
   }
   static void sleep(void *cookie,uint64_t ns) {auto &d=of(cookie);std::lock_guard<std::mutex> lock(d.mutex);d.time+=ns;}
   static void release_range(void *cookie,uint64_t start,uint64_t size) {
      auto &d=of(cookie);std::lock_guard<std::mutex> lock(d.mutex);d.released.emplace_back(start,size);
   }
   tu_darwin_io io(){return {this,open,close,call,map,unmap,now,sleep};}
   void complete(uint32_t fence){std::lock_guard<std::mutex> lock(mutex);for(auto &[id,p]:ports){(void)id;if(p.submitted.count(fence))p.done.insert(fence);}}
};
struct Device {
   Driver &driver;tu_darwin_backend *backend=nullptr;uint64_t base=0;
   explicit Device(Driver &d):driver(d) {
      tu_darwin_connection *c=nullptr;check(!tu_darwin_connection::create(d.io(),&c));base=c->info().va_start;
      check(!tu_darwin_backend::create(c,Driver::release_range,&d,&backend));c->release();
   }
   ~Device(){delete backend;}
   tu_darwin_bo bo(unsigned index=0,bool host=false) {
      tu_darwin_bo result;check(!backend->new_bo(base+uint64_t(index)*8192,8192,host?reinterpret_cast<void *>(0x20000000):nullptr,false,&result));return result;
   }
   uint32_t submit(const tu_darwin_bo &bo) {
      NativeAdrenoAbiSubmitBuffer entry{bo.handle,bo.iova,4};uint32_t fence=0;
      check(!backend->submit(&entry,1,&fence,driver.time+5000000000ULL));check(fence);return fence;
   }
};

static void lifecycle()
{
   for(bool tunnel:{false,true}) {
      Driver d;d.tunnel=tunnel;
      auto *a=new Device(d);auto first=a->bo();uint32_t fence=a->submit(first);
      tu_darwin_connection *probe=nullptr;check(!tu_darwin_connection::create(d.io(),&probe));probe->release();
      check(d.ports.size()==1);check(a->backend->wait_fence(fence,0)==kIOReturnTimeout);
      auto *b=new Device(d);auto second=b->bo();check(first.handle==second.handle && first.owner!=second.owner && first.iova!=second.iova);
      uint64_t ranges[]={0x20000000,8192},covered=99;
      check(!tu_darwin_sync_host_batch(ranges,1,2,&covered) && covered==0 && d.sync_calls==0);
      check(tu_darwin_sync_host_range(ranges[0],ranges[1],2)==kIOReturnNotFound);
      delete a;check(d.ports.size()==1);
      check(!tu_darwin_sync_host_batch(ranges,1,2,&covered) && covered==1 && d.last_phase==2);
      check(!tu_darwin_sync_host_range(ranges[0],ranges[1],1));
      d.open_error=kIOReturnNoSpace;check(tu_darwin_connection::create(d.io(),&probe)==kIOReturnNoSpace && !probe);d.open_error=0;
      check(d.ports.size()==1 && !b->backend->check_status());
      tu_darwin_backend *duplicate=nullptr;
      check(tu_darwin_backend::create(b->backend->connection(),Driver::release_range,&d,&duplicate)==kIOReturnBusy && !duplicate);
      check(!tu_darwin_sync_host_batch(ranges,1,2,&covered) && covered==1);
      delete b;check(d.ports.empty());check(!tu_darwin_connection::unique_live());
   }
   Driver bad;bad.short_reply=NativeAdrenoMethodGetDeviceInfo;tu_darwin_connection *c=nullptr;
   check(tu_darwin_connection::create(bad.io(),&c)==kIOReturnUnderrun && !c && bad.ports.empty());
}
static void retirement()
{
   Driver d;Device device(d);auto bo=device.bo();auto fence=device.submit(bo);
   check(!device.backend->retire_bo(bo));check(d.released.empty() && !d.free_calls);
   d.complete(fence);check(!device.backend->collect());check(d.released.size()==1 && d.free_calls==1);
   check(!device.backend->collect() && d.free_calls==1);
   auto imported=device.bo(1,true);check(!device.backend->retire_bo(imported));check(d.cpu_unmaps==0 && d.released.size()==2);
   auto busy=device.bo(2);d.free_busy=true;check(!device.backend->discard_bo(busy));check(d.released.size()==2);
   d.free_busy=false;check(!device.backend->collect() && d.released.size()==3);
   d.alloc_error=kIOReturnNoMemory;tu_darwin_bo failed;
   check(device.backend->new_bo(device.base+32768,8192,nullptr,false,&failed)==kIOReturnNoMemory);
   check(!failed.handle && !device.backend->failure() && d.released.size()==4);d.alloc_error=0;
   d.map_error=kIOReturnNoMemory;d.free_busy=true;
   check(device.backend->new_bo(device.base+40960,8192,nullptr,false,&failed)==kIOReturnNoMemory);
   check(!failed.handle && d.released.size()==4);d.map_error=0;d.free_busy=false;
   check(!device.backend->collect() && d.released.size()==5);
}
static void mapping()
{
   {Driver d;Device device(d);auto bo=device.bo();check(!device.backend->map_bo(&bo,nullptr));check(bo.mapping && bo.token);
    check(!device.backend->retire_bo(bo));check(d.cpu_unmaps==1 && d.native_unmaps==1 && d.released.size()==1);}
   {Driver d;Device device(d);auto bo=device.bo();d.short_mapping=true;
    check(device.backend->map_bo(&bo,nullptr)==kIOReturnUnderrun);check(!bo.mapping && !bo.token && d.cpu_unmaps==1 && d.native_unmaps==1);
    check(!device.backend->failure());check(!device.backend->retire_bo(bo) && d.released.size()==1);}
   {Driver d;Device device(d);auto bo=device.bo();d.cpu_map_error=kIOReturnNoMemory;
    check(device.backend->map_bo(&bo,nullptr)==kIOReturnNoMemory && !bo.mapping && !bo.token && d.native_unmaps==1);
    d.cpu_map_error=0;check(!device.backend->map_bo(&bo,nullptr));check(!device.backend->retire_bo(bo));}
   {Driver d;Device device(d);auto bo=device.bo();d.short_mapping=true;d.unmap_error=kIOReturnIOError;
    check(device.backend->map_bo(&bo,nullptr)==kIOReturnUnderrun);check(bo.mapping && bo.token && device.backend->failure());
    check(device.backend->retire_bo(bo)!=0 && d.released.empty() && !d.free_calls);}
   {Driver d;Device device(d);auto bo=device.bo();d.short_mapping=true;d.lease_error=kIOReturnIOError;
    check(device.backend->map_bo(&bo,nullptr)==kIOReturnUnderrun);check(!bo.mapping && bo.token && device.backend->failure());}
   {Driver d;Device device(d);auto bo=device.bo();d.wrong_placement=true;
    check(device.backend->map_bo(&bo,reinterpret_cast<void *>(0x50000000))==kIOReturnUnderrun);
    check(!bo.mapping && !bo.token && d.cpu_unmaps==1);}
}
static void admission()
{
   Driver d;Device device(d);auto bo=device.bo();d.busy_until=d.time+50000000ULL;
   auto first=device.submit(bo);check(first && d.time>=d.busy_until && !device.backend->failure());
   d.complete(first);
   std::vector<uint32_t> fences;
   for(unsigned i=0;i<8;i++)fences.push_back(device.submit(bo));
   d.complete(fences[0]);auto ninth=device.submit(bo);check(ninth && d.submits==10);
   d.auto_complete=true;device.submit(bo); // waits for a real pending fence, never drops an unresolved one
   std::vector<NativeAdrenoAbiSubmitBuffer> large(129,{bo.handle,bo.iova,4});uint32_t out=9;unsigned before=d.submits;
   check(device.backend->submit(large.data(),large.size(),&out,d.time+1000000)==kIOReturnNoResources && !out && d.submits==before);
   d.next_fence=0x80000001u;d.auto_complete=true;
   auto high=device.submit(bo);check(high==0x80000001u);d.complete(high);check(!device.backend->wait_fence(high,0));
   check(tu_darwin_deadline(UINT64_MAX-3,10)==UINT64_MAX);
   Driver timeout;Device blocked(timeout);auto buffer=blocked.bo();timeout.busy_until=UINT64_MAX;
   NativeAdrenoAbiSubmitBuffer e{buffer.handle,buffer.iova,4};
   check(blocked.backend->submit(&e,1,&out,timeout.time+5000000)==kIOReturnTimeout && !out && blocked.backend->failure());
}
static void syncs()
{
   Driver d;Device device(d);tu_darwin_sync a,b;
   device.backend->sync_init(&a,false);device.backend->sync_init(&b,false);
   check(device.backend->sync_wait(&a,true,0)==kIOReturnTimeout); // pending is not unconditional success
   uint64_t deadline=d.time+1000000000ULL;d.block_clock=true;
   auto waiter=std::async(std::launch::async,[&]{return device.backend->sync_wait(&a,true,deadline);});
   {std::unique_lock<std::mutex> lock(d.mutex);d.condition.wait(lock,[&]{return d.clock_inside;});}
   auto signal=std::async(std::launch::async,[&]{return device.backend->sync_signal(&a,0);});
   {std::lock_guard<std::mutex> lock(d.mutex);d.release_clock=true;d.condition.notify_all();}
   check(!signal.get() && !waiter.get());
   check(!device.backend->sync_move(&b,&a));check(device.backend->sync_wait(&a,true,0)==kIOReturnTimeout);
   check(!device.backend->sync_wait(&b,false,0));check(!device.backend->sync_reset(&b));
   auto bo=device.bo();auto fence=device.submit(bo);check(!device.backend->sync_signal(&b,fence));
   check(!device.backend->sync_wait(&b,true,0));check(device.backend->sync_wait(&b,false,0)==kIOReturnTimeout);
   d.complete(fence);check(!device.backend->sync_wait(&b,false,0));
   tu_darwin_sync *list[]={&a,&b};check(!device.backend->sync_wait_many(list,2,true,false,0));
   check(device.backend->sync_wait_many(list,2,false,false,0)==kIOReturnTimeout);
   check(!device.backend->sync_wait_many(nullptr,0,true,false,0));
   device.backend->sync_reset(&b);fence=device.submit(bo);device.backend->sync_signal(&b,fence);d.wait_error=kIOReturnIOError;
   check(device.backend->sync_wait_many(list,2,true,false,UINT64_MAX)==kIOReturnIOError);
   check(device.backend->sync_wait(&a,true,UINT64_MAX)==kIOReturnIOError); // loss wakes/refuses pending waits
}
static void close_during_export()
{
   Driver d;auto *device=new Device(d);device->bo(0,true);d.block_sync=true;
   auto exported=std::async(std::launch::async,[&]{uint64_t ranges[]={0x20000000,8192},covered=0;
      check(!tu_darwin_sync_host_batch(ranges,1,2,&covered));check(covered==1);});
   {std::unique_lock<std::mutex> lock(d.mutex);d.condition.wait(lock,[&]{return d.sync_inside;});}
   delete device;
   {std::lock_guard<std::mutex> lock(d.mutex);check(d.ports.size()==1 && d.closed.empty());d.release_sync=true;d.condition.notify_all();}
   exported.get();check(d.ports.empty() && d.closed.size()==1);
}
static void capacity_and_wire()
{
   {Driver d;Device device(d);d.free_busy=true;
    for(unsigned i=0;i<256;i++)check(!device.backend->retire_bo(device.bo(i)));
    check(device.backend->retire_bo(device.bo(256))==kIOReturnNoResources && d.released.empty());}
   {Driver d;Device device(d);d.short_reply=NativeAdrenoMethodBoAlloc;tu_darwin_bo bo;
    check(device.backend->new_bo(device.base,8192,nullptr,false,&bo)==kIOReturnUnderrun && !bo.handle);
    check(device.backend->failure() && d.released.size()==1);}
   {Driver d;Device device(d);uint64_t bad[]={UINT64_MAX-4095,4096},covered=~0ULL;unsigned before=d.calls;
    check(tu_darwin_sync_host_batch(bad,1,2,&covered)==kIOReturnBadArgument && !covered && d.calls==before);}
}
int main()
{
   lifecycle();retirement();mapping();admission();syncs();close_during_export();capacity_and_wire();
   {
      Driver d;d.input_bits=36;d.base_override=0x110000000ULL;d.window_bytes=256ULL<<20;Device wide(d);
      auto bo=wide.bo();check(bo.iova==d.base_override && wide.backend->connection()->params().input_bits==36);
      auto fence=wide.submit(bo);check(!wide.backend->retire_bo(bo) && d.released.empty());
      d.complete(fence);check(!wide.backend->collect() && d.released.size()==1 && d.released[0].first==d.base_override);
   }
   check(!tu_darwin_connection::unique_live());
   std::puts("PASS actual backend: isolated devices/probes, retained export close race, BO/CPU-map rollback, fence-owned retirement, foreign-busy admission, bounded batches, pending/move/ANY/loss synchronization and capacity failures");
   return 0;
}
