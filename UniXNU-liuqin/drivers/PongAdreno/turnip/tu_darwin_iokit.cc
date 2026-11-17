/* SPDX-License-Identifier: MIT */
#include "tu_darwin_core.h"
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int32_t open_port(void *, tu_darwin_port *out)
{
   *out={};
   const char *name=getenv("TU_DARWIN_SERVICE");
   if (!name || !*name) name="IOAcceleratorES";
   const bool direct=!strcmp(name,"NativeAdrenoGpuPublished");
   uint32_t type=direct?0:0x4147;
   const char *text=getenv("TU_DARWIN_OPEN_TYPE");
   if (text && *text) {
      char *end=nullptr;errno=0;unsigned long value=strtoul(text,&end,0);
      if (errno || !end || *end || value>UINT32_MAX) return kIOReturnBadArgument;
      type=uint32_t(value);
   }
   io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching(name));
   if (!service) return kIOReturnNotFound;
   io_connect_t port=0;
   const kern_return_t status=IOServiceOpen(service,mach_task_self(),type,&port);
   IOObjectRelease(service);
   if (!status) *out={port,!direct};
   return status;
}
static int32_t close_port(void *, uint32_t port) { return IOServiceClose(port); }
static int32_t call_port(void *, uint32_t port, uint32_t selector, const void *in, size_t bytes,
                         void *out, size_t *received)
{
   return IOConnectCallStructMethod(port,selector,in,bytes,out,received);
}
static int32_t map_port(void *, uint32_t port, uint32_t token, void *placed, void **out, uint64_t *bytes)
{
   mach_vm_address_t address=uintptr_t(placed);mach_vm_size_t length=0;
   uint32_t options=(placed?0:kIOMapAnywhere)|kIOMapWriteCombineCache;
   int32_t status=IOConnectMapMemory64(port,token,mach_task_self(),&address,&length,options);
   *out=nullptr;*bytes=0;
   if (!status) {*out=reinterpret_cast<void *>(uintptr_t(address));*bytes=length;}
   return status;
}
static int32_t unmap_port(void *, uint32_t port, uint32_t token, void *address)
{
   return IOConnectUnmapMemory64(port,token,mach_task_self(),uintptr_t(address));
}
static uint64_t now_ns(void *)
{
   struct timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
   return uint64_t(now.tv_sec)*1000000000ULL+uint64_t(now.tv_nsec);
}
static void sleep_ns(void *, uint64_t ns)
{
   struct timespec delay{time_t(ns/1000000000ULL),long(ns%1000000000ULL)};
   while (nanosleep(&delay,&delay) && errno==EINTR) {}
}
const tu_darwin_io &tu_darwin_iokit_io()
{
   static const tu_darwin_io io{nullptr,open_port,close_port,call_port,map_port,unmap_port,now_ns,sleep_ns};
   return io;
}
