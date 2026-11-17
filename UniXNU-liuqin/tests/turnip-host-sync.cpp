// SPDX-License-Identifier: MIT
// Compiled with the extracted actual Darwin export bodies. No device/GPU.
#include "../drivers/PongAdreno/submit/NativeAdrenoABI.h"
#include "tu_darwin_host_sync.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <initializer_list>
using kern_return_t=uint32_t;
constexpr kern_return_t KERN_SUCCESS=0,kIOReturnBadArgument=0xe00002c2u,kIOReturnNotReady=0xe00002d8u;
static struct {bool open=false;pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;} dw;
static unsigned calls=0;static kern_return_t injected=0;
static NativeAdrenoAbiSyncHostRange recordedRange{};
static NativeAdrenoAbiSyncHostBatch recordedBatch{};
static kern_return_t dw_call(uint32_t method,const void *input,size_t size,void *output,size_t outsize) {
 calls++;
 assert(pthread_mutex_trylock(&dw.lock)!=0);
 if(method==NativeAdrenoMethodSyncHostRange){assert(size==sizeof(recordedRange));memcpy(&recordedRange,input,size);}
 else {assert(method==NativeAdrenoMethodSyncHostBatch && size==sizeof(recordedBatch));memcpy(&recordedBatch,input,size);
  assert(outsize==sizeof(NativeAdrenoAbiSyncHostBatchReply));
  static_cast<NativeAdrenoAbiSyncHostBatchReply *>(output)->covered=UINT64_MAX;
 }
 return injected;
}
#include "exports.inc"
int main(){
 uint64_t ranges[128]={},covered=99;
 for(unsigned i=0;i<64;i++){ranges[2*i]=0x100000000ull+4096*i;ranges[2*i+1]=4096;}
 assert((uint32_t)tu_darwin_sync_host_range(ranges[0],4096,2)==kIOReturnNotReady && calls==0);
 assert((uint32_t)tu_darwin_sync_host_batch(ranges,1,2,&covered)==kIOReturnNotReady && !covered);
 dw.open=true;
 assert(tu_darwin_sync_host_range(ranges[0],4096,2)==0 && recordedRange.after==2 && recordedRange.reserved==0);
 assert(tu_darwin_sync_host_batch(ranges,64,1,&covered)==0 && covered==UINT64_MAX && recordedBatch.after==1 && recordedBatch.count==64);
 assert(tu_darwin_sync_host_batch(ranges,3,2,&covered)==0 && covered==7);
 injected=0xe00002c7u;
 assert((uint32_t)tu_darwin_sync_host_batch(ranges,1,2,&covered)==injected && covered==0);
 unsigned before=calls;
 for(unsigned count:{0u,65u,~0u})assert((uint32_t)tu_darwin_sync_host_batch(ranges,count,2,&covered)==kIOReturnBadArgument);
 assert((uint32_t)tu_darwin_sync_host_batch(ranges,1,3,&covered)==kIOReturnBadArgument);
 assert((uint32_t)tu_darwin_sync_host_range(UINT64_MAX-4095,8192,1)==kIOReturnBadArgument);
 assert((uint32_t)tu_darwin_sync_host_range(ranges[0]+1,4096,1)==kIOReturnBadArgument);
 assert((uint32_t)tu_darwin_sync_host_batch(ranges,1,1,nullptr)==kIOReturnBadArgument);
 assert(calls==before);
 puts("PASS exact host sync ABI, lock, full/partial coverage, unavailable/unsupported result and bounded validation");
}
