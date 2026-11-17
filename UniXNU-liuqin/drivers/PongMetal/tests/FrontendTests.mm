// SPDX-License-Identifier: MIT
// Exercise the real Objective-C frontend with a recording C ABI. Unexpected
// runtime calls trap; these tests make no claim about Vulkan/GPU execution.
#include "../NativeBridgeMetalFrontend.mm"
#include <cassert>
#include <cstdlib>
#undef assert
#define assert(value) do {if(!(value)){std::fprintf(stderr,"FAIL assertion: %s\n",#value);std::_Exit(1);}} while(0)

struct NmbTexture {NmbTextureDescriptor descriptor;};
struct NmbCommandBuffer {uint64_t status=MTLCommandBufferStatusCompleted;};
struct NmbEncoder {bool blit;};
struct NmbLibrary {std::vector<std::string> externs;};
struct NmbBuffer {void (*release)(void *,void *,uint64_t);void *context,*pointer;uint64_t length;};
static unsigned importCalls=0,bufferReleases=0;
static const char *mockError="injected runtime refusal";
static NmbRenderPassDescriptor recordedPass;
static std::vector<NmbAttachmentCopy> copies;
static std::vector<std::string> events;
static uint64_t recordedAction,recordedOptions;
static unsigned recordedAttachment,storeCalls,optionCalls,renderCalls,hostCopies;
static bool acceptStore=true,acceptEnd=true;

extern "C" const char *nmb_last_error(void) {return mockError;}
extern "C" uint32_t nmb_library_extern_count(const NmbLibrary *l) {return (uint32_t)l->externs.size();}
extern "C" size_t nmb_library_extern_name(const NmbLibrary *l,uint32_t i,char *out,size_t size) {
 const auto &name=l->externs.at(i);if(out && size)strlcpy(out,name.c_str(),size);return name.size();
}
extern "C" void nmb_library_release(NmbLibrary *l) {delete l;}
extern "C" NmbBuffer *nmb_buffer_new_no_copy_releasing(NmbDevice *,void *pointer,uint64_t length,uint64_t,
 void (*release)(void *,void *,uint64_t),void *context) {
 importCalls++;if(importCalls==1){mockError="no-copy buffer: OutOfMemory(Device)";return nullptr;}
 return new NmbBuffer{release,context,pointer,length};
}
extern "C" void nmb_buffer_release(NmbBuffer *b) {
 if(!b)return;
 bufferReleases++;if(b->release)b->release(b->context,b->pointer,b->length);delete b;
}
extern "C" uint64_t nmb_buffer_length(const NmbBuffer *b) {return b->length;}
extern "C" void nmb_texture_release(NmbTexture *t) {delete t;}
extern "C" void nmb_command_buffer_release(NmbCommandBuffer *c) {delete c;}
extern "C" uint64_t nmb_command_buffer_status(const NmbCommandBuffer *c) {return c->status;}
extern "C" void nmb_texture_describe(const NmbTexture *t,NmbTextureDescriptor *out) {*out=t->descriptor;}
extern "C" NmbTexture *nmb_texture_new_view(NmbTexture *t,uint64_t,uint64_t,const uint64_t *,const uint64_t *,bool) {
 return new NmbTexture{t->descriptor};
}
extern "C" NmbEncoder *nmb_render_encoder_new(NmbCommandBuffer *,const NmbRenderPassDescriptor *pass) {
 recordedPass=*pass;renderCalls++;events.push_back("render");return new NmbEncoder{false};
}
extern "C" NmbEncoder *nmb_blit_encoder_new(NmbCommandBuffer *) {events.push_back("blit");return new NmbEncoder{true};}
extern "C" bool nmb_encoder_end(NmbEncoder *e) {
 events.push_back(e->blit?"blit-end":"render-end");delete e;return acceptEnd;
}
extern "C" void nmb_encoder_discard(NmbEncoder *e) {delete e;}
extern "C" bool nmb_blit_copy_attachment(NmbEncoder *e,const NmbAttachmentCopy *copy) {
 assert(e->blit);copies.push_back(*copy);events.push_back("copy");return true;
}
static bool store(unsigned attachment,uint64_t action) {
 recordedAttachment=attachment;recordedAction=action;storeCalls++;return acceptStore;
}
static bool options(unsigned attachment,uint64_t value) {
 recordedAttachment=attachment;recordedOptions=value;optionCalls++;return acceptStore;
}
extern "C" bool nmb_render_set_color_store_action(NmbEncoder *,uint64_t action,uint32_t index) {return store(index,action);}
extern "C" bool nmb_render_set_depth_store_action(NmbEncoder *,uint64_t action) {return store(8,action);}
extern "C" bool nmb_render_set_stencil_store_action(NmbEncoder *,uint64_t action) {return store(9,action);}
extern "C" bool nmb_render_set_color_store_action_options(NmbEncoder *,uint64_t value,uint32_t index) {return options(index,value);}
extern "C" bool nmb_render_set_depth_store_action_options(NmbEncoder *,uint64_t value) {return options(8,value);}
extern "C" bool nmb_render_set_stencil_store_action_options(NmbEncoder *,uint64_t value) {return options(9,value);}
extern "C" bool nmb_texture_replace(NmbTexture *,const uint64_t *,uint64_t,uint64_t,const void *,uint64_t,uint64_t,uint64_t) {
 hostCopies++;return true;
}
extern "C" bool nmb_texture_get_bytes(const NmbTexture *,void *,uint64_t,uint64_t,uint64_t,const uint64_t *,uint64_t,uint64_t) {
 hostCopies++;return true;
}
extern "C" void sys_dcache_flush(void *,size_t) {}

static void reset() {
 copies.clear();events.clear();storeCalls=optionCalls=renderCalls=hostCopies=0;acceptStore=acceptEnd=true;
}
static NBTexture *texture(uint64_t width=64,uint64_t height=64,MTLPixelFormat format=MTLPixelFormatBGRA8Unorm,
                         MTLTextureType type=MTLTextureType2D,uint64_t layers=1,uint64_t depth=1) {
 NBTexture *t=[NBTexture new];
 t.descriptor={(uint64_t)type,(uint64_t)format,width,height,depth,4,1,layers,MTLTextureUsageRenderTarget,MTLResourceStorageModePrivate,0};
 t.handle=new NmbTexture{t.descriptor};return t;
}
static NBCommandBuffer *command() {NBCommandBuffer *c=[NBCommandBuffer new];c.handle=new NmbCommandBuffer;return c;}
static MTLRenderPassDescriptor *pass(MTLStoreAction action=MTLStoreActionUnknown,MTLLoadAction load=MTLLoadActionLoad) {
 MTLRenderPassDescriptor *d=[MTLRenderPassDescriptor renderPassDescriptor];
 d.colorAttachments[0].texture=(id)texture();d.colorAttachments[0].resolveTexture=(id)texture();
 d.colorAttachments[0].loadAction=load;d.colorAttachments[0].storeAction=action;return d;
}
static id<MTLRenderCommandEncoder> encoder(NBCommandBuffer *c,MTLRenderPassDescriptor *d) {
 id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:d];assert(e && !c.encodeFailure);return e;
}

static void lateResolve() {
 reset();NBCommandBuffer *c=command();MTLRenderPassDescriptor *d=pass();
 d.renderTargetWidth=40;d.renderTargetHeight=48;
 d.colorAttachments[1].texture=(id)texture(64,32);
 d.colorAttachments[0].storeActionOptions=MTLStoreActionOptionCustomSamplePositions;
 id<MTLRenderCommandEncoder> e=encoder(c,d);
 assert(recordedPass.colors[0].texture==[(NBTexture *)d.colorAttachments[0].texture handle]);
 assert(recordedPass.colors[0].store==MTLStoreActionUnknown && recordedPass.colors[0].store_options==1);
 [e setColorStoreAction:MTLStoreActionStoreAndMultisampleResolve atIndex:0];
 assert(recordedAction==MTLStoreActionStore && recordedAttachment==0 && copies.empty());
 [e endEncoding];assert(!c.encodeFailure && copies.size()==1);
 assert(copies[0].size[0]==40 && copies[0].size[1]==32 && copies[0].aspect==0);
 assert((events==std::vector<std::string>{"render","render-end","blit","copy","blit-end"}));
 puts("PASS late resolve, initial options, source Load, render-area clamp and ordering");
}
static void noPrematureResolve() {
 for(unsigned mode=0;mode<5;mode++) {
  reset();NBCommandBuffer *c=command();id<MTLRenderCommandEncoder> e=encoder(c,pass());
  if(mode==1)[e setColorStoreAction:MTLStoreActionStore atIndex:0];
  if(mode==2){acceptStore=false;[e setColorStoreAction:MTLStoreActionMultisampleResolve atIndex:0];}
  if(mode==3){[e setColorStoreAction:MTLStoreActionMultisampleResolve atIndex:0];acceptEnd=false;}
  if(mode==4) {
   [e setColorStoreAction:MTLStoreActionMultisampleResolve atIndex:0];
   [e setColorStoreAction:MTLStoreActionDontCare atIndex:0];
   [e setColorStoreAction:MTLStoreActionMultisampleResolve atIndex:0];
   assert(recordedAction==MTLStoreActionMultisampleResolve); // handed back to runtime, no resurrected plan
  }
  [e endEncoding];assert(copies.empty());
  assert(events.size()==2 && events.back()=="render-end");
  if(mode==2 || mode==3)assert(c.encodeFailure);
 }
 puts("PASS unresolved, non-resolving, refused and failed-end paths never copy");
}
static void knownActions() {
 for(bool direct:{false,true}) {
  reset();NBCommandBuffer *c=command();
  MTLRenderPassDescriptor *d=pass(direct?MTLStoreActionMultisampleResolve:MTLStoreActionStoreAndMultisampleResolve,MTLLoadActionClear);
  id<MTLRenderCommandEncoder> e=encoder(c,d);
  assert(recordedPass.colors[0].store==MTLStoreActionStore);
  assert(recordedPass.colors[0].texture==[(NBTexture *)(direct?d.colorAttachments[0].resolveTexture:d.colorAttachments[0].texture) handle]);
  [e setColorStoreAction:MTLStoreActionDontCare atIndex:0];[e endEncoding];
  assert(copies.size()==(direct?0:1)); // known actions stay the descriptor's
 }
 puts("PASS known direct-target and source-retaining paths preserved");
}
static void aspectsAndOptions() {
 reset();NBCommandBuffer *c=command();MTLRenderPassDescriptor *d=pass();
 NBTexture *source=texture(64,64,MTLPixelFormatDepth32Float_Stencil8);
 NBTexture *destination=texture(64,64,MTLPixelFormatDepth32Float_Stencil8);
 for(MTLRenderPassAttachmentDescriptor *a:@[d.depthAttachment,d.stencilAttachment]) {
  a.texture=(id)source;a.resolveTexture=(id)destination;a.storeAction=MTLStoreActionUnknown;
  a.storeActionOptions=MTLStoreActionOptionCustomSamplePositions;
 }
 id<MTLRenderCommandEncoder> e=encoder(c,d);
 assert(recordedPass.depth.store_options==1 && recordedPass.stencil.store_options==1);
 [e setColorStoreActionOptions:MTLStoreActionOptionCustomSamplePositions atIndex:0];
 assert(optionCalls==1 && recordedOptions==1 && recordedAttachment==0);
 [e setDepthStoreActionOptions:MTLStoreActionOptionNone];assert(recordedOptions==0 && recordedAttachment==8);
 [e setStencilStoreActionOptions:MTLStoreActionOptionNone];assert(recordedOptions==0 && recordedAttachment==9);
 [e setDepthStoreAction:MTLStoreActionMultisampleResolve];assert(recordedAttachment==8 && recordedAction==MTLStoreActionStore);
 [e setStencilStoreAction:MTLStoreActionStore];assert(recordedAttachment==9);
 [e endEncoding];assert(copies.size()==1 && copies[0].aspect==1);
 reset();c=command();e=encoder(c,d);
 [e setStencilStoreAction:MTLStoreActionMultisampleResolve];[e endEncoding];
 assert(copies.size()==1 && copies[0].aspect==2);
 puts("PASS all six selectors and independently selected depth/stencil copies");
}
static void bounds() {
 for(bool option:{false,true}) {
  reset();NBCommandBuffer *c=command();id<MTLRenderCommandEncoder> e=encoder(c,pass());
  if(option)[e setColorStoreActionOptions:MTLStoreActionOptionNone atIndex:(1ull<<32)];
  else [e setColorStoreAction:MTLStoreActionStore atIndex:(1ull<<32)];
  assert(c.encodeFailure && storeCalls==0 && optionCalls==0);[e endEncoding];assert(copies.empty());
 }
 for(unsigned kind=0;kind<5;kind++) {
  reset();NBCommandBuffer *c=command();MTLRenderPassDescriptor *d=pass();
  if(kind==0)d.colorAttachments[0].level=64;
  if(kind==1)d.colorAttachments[0].resolveLevel=64;
  if(kind==2)d.renderTargetArrayLength=UINT64_MAX;
  if(kind==3)d.colorAttachments[0].resolveTexture=(id)texture(32,64);
  if(kind==4)d.colorAttachments[0].resolveTexture=(id)texture(64,64,MTLPixelFormatR8Unorm);
  id<MTLRenderCommandEncoder> refused=[c renderCommandEncoderWithDescriptor:d];
  assert(refused && ![(NBEncoder *)refused handle] && c.encodeFailure && renderCalls==0);
  assert([(id)refused methodForSelector:@selector(setColorStoreAction:atIndex:)]);
  [refused endEncoding];assert(copies.empty());
 }
 reset();NBCommandBuffer *c=command();MTLRenderPassDescriptor *d=pass();
 d.colorAttachments[0].texture=(id)texture(64,64,MTLPixelFormatBGRA8Unorm,MTLTextureType2DArray,4);
 d.colorAttachments[0].resolveTexture=(id)texture(64,64,MTLPixelFormatBGRA8Unorm,MTLTextureType2DArray,2);
 d.colorAttachments[0].slice=1;d.colorAttachments[0].resolveSlice=1;d.renderTargetArrayLength=3;
 id<MTLRenderCommandEncoder> e=encoder(c,d);[e setColorStoreAction:MTLStoreActionMultisampleResolve atIndex:0];[e endEncoding];
 assert(copies.size()==1 && copies[0].source_slice==1 && copies[0].destination_slice==1);
 reset();c=command();d=pass();
 d.colorAttachments[0].texture=(id)texture(64,64,MTLPixelFormatBGRA8Unorm,MTLTextureType3D,1,4);
 d.colorAttachments[0].resolveTexture=(id)texture(64,64,MTLPixelFormatBGRA8Unorm,MTLTextureType3D,1,4);
 d.colorAttachments[0].slice=UINT64_MAX;d.colorAttachments[0].depthPlane=1;
 d.colorAttachments[0].resolveDepthPlane=2;d.renderTargetArrayLength=2;
 e=encoder(c,d);[e setColorStoreAction:MTLStoreActionMultisampleResolve atIndex:0];[e endEncoding];
 assert(copies.size()==2 && copies[0].source_slice==0 && copies[1].source_slice==0);
 assert(copies[1].source_origin[2]==2 && copies[1].destination_origin[2]==3);
 puts("PASS index/mip/layer/format bounds, callable refused encoders and volume planes");
}

@interface TestDrawable : NSObject
@property(nonatomic) unsigned presentations;
@end
@implementation TestDrawable
- (void)present {_presentations++;}
- (void)presentAtTime:(CFTimeInterval)time {(void)time;_presentations++;}
- (void)presentAfterMinimumDuration:(CFTimeInterval)time {(void)time;_presentations++;}
@end
static void presentation() {
 for(unsigned kind=0;kind<3;kind++) {
  NBCommandBuffer *c=command();TestDrawable *drawable=[TestDrawable new];
  [c presentDrawable:(id)drawable];[c presentDrawable:(id)drawable atTime:1];[c presentDrawable:(id)drawable afterMinimumDuration:1];
  if(kind==0)c.handle->status=MTLCommandBufferStatusError;
  if(kind==1)c.failedLocally=YES;
  for(MTLCommandBufferHandler handler in c.completedHandlers)handler((id)c);
  assert(drawable.presentations==(kind==2?3:0));
 }
 puts("PASS presentation requires successful completion for all three selectors");
}
@interface NBCommandBuffer (TestHandoff)
- (void)testHandoff:(mach_port_t)port;
@end
@implementation NBCommandBuffer (TestHandoff)
- (void)testHandoff:(mach_port_t)port {displayHandoffs.push_back(port);}
@end
static uint64_t handoffFailure;
static kern_return_t handoffCall(unsigned,unsigned selector,const uint64_t *input,unsigned count,uint64_t *,unsigned *) {
 assert(selector==0x504f4701u && count==3 && input[1]==1);handoffFailure=input[2];return KERN_SUCCESS;
}
static void failedHandoff() {
 NBCommandBuffer *c=command();mach_port_t port=MACH_PORT_NULL;
 assert(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&port)==KERN_SUCCESS);
 assert(mach_port_insert_right(mach_task_self(),port,port,MACH_MSG_TYPE_MAKE_SEND)==KERN_SUCCESS);
 pthread_once(&pongsurface::once,[]{});
 pongsurface::connection=1;pongsurface::call=handoffCall;[c testHandoff:port];
 c.failedLocally=YES;
 dispatch_group_enter(c.handlers);dispatch_group_enter(c.schedulingHandlers);
 [c finish];assert(handoffFailure==1);
 assert(dispatch_group_wait(c.handlers,DISPATCH_TIME_NOW)==0);
 mach_port_destroy(mach_task_self(),port);
 puts("PASS local refusal returns failed IOSurface ownership and releases waiters");
}
static void surfaceAndSpan() {
 reset();NBTexture *t=texture();t.surfacePlane=1;
 id<MTLTexture> view=[t newTextureViewWithPixelFormat:MTLPixelFormatBGRA8Unorm];
 assert(view.iosurfacePlane==1 && view.parentTexture==(id)t);
 uint8_t bytes[64]={};const MTLRegion r=MTLRegionMake3D(0,0,0,4,2,2);
 [t replaceRegion:r mipmapLevel:0 slice:0 withBytes:bytes bytesPerRow:8 bytesPerImage:16];assert(hostCopies==1);
 [t getBytes:bytes bytesPerRow:8 bytesPerImage:16 fromRegion:r mipmapLevel:0 slice:0];assert(hostCopies==2);
 [t replaceRegion:r mipmapLevel:0 slice:0 withBytes:bytes bytesPerRow:NSUIntegerMax bytesPerImage:16];
 [t getBytes:bytes bytesPerRow:8 bytesPerImage:NSUIntegerMax fromRegion:r mipmapLevel:0 slice:0];
 [t replaceRegion:r mipmapLevel:0 withBytes:bytes bytesPerRow:NSUIntegerMax];
 [t getBytes:bytes bytesPerRow:NSUIntegerMax fromRegion:r mipmapLevel:0];assert(hostCopies==2);
 puts("PASS view plane identity and byte-span overflow rejection before C ABI calls");
}
static void libraryAndPressure() {
 NBLibrary *library=[NBLibrary new];library.handle=new NmbLibrary{{"_add",std::string(600,'x')}};
 NSArray *names=[library externFunctionNames];assert(names.count==2 && [names[0] isEqualToString:@"_add"] && [names[1] length]==600);
 NBLibrary *empty=[NBLibrary new];empty.handle=new NmbLibrary{};assert([empty externFunctionNames].count==0);
 IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)@{(id)kIOSurfaceWidth:@4,(id)kIOSurfaceHeight:@4,
  (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@16,(id)kIOSurfaceAllocSize:@4096});assert(surface);
 importCalls=bufferReleases=0;
 @autoreleasepool {
  // Cache owns the retired buffer; eviction must release outside its lock.
  NBBuffer *retired=[NBBuffer new];retired.handle=new NmbBuffer{nullptr,nullptr,nullptr,4096};
  gHeldImports[{999,1,4096}]={retired,nowUs(),4096};
 }
 // No need to fabricate a live GPU owner: a failed import is sufficient to
 // exercise the retry and callback ownership of the actual surface path.
 @autoreleasepool {
  NBBuffer *buffer=surfaceBuffer(surface,IOSurfaceGetBaseAddress(surface),IOSurfaceGetAllocSize(surface));
  assert(buffer && importCalls==2 && bufferReleases==1);
 }
 trimHeldImports(true);assert(bufferReleases==2);CFRelease(surface);
 mockError="injected runtime refusal";
 puts("PASS extern names including long names/empty table, surface OOM retry and deallocator ownership");
}
static void hostSyncCoverage() {
 const char *path=getenv("PONG_TEST_SYNC_LIB");assert(path);
 void *library=dlopen(path,RTLD_NOW|RTLD_LOCAL);assert(library);gLoaderPath=path;
 auto *calls=(unsigned *)dlsym(library,"pong_test_sync_calls");
 auto *phase=(uint32_t *)dlsym(library,"pong_test_sync_phase");
 auto *result=(int32_t *)dlsym(library,"pong_test_sync_result");
 auto *coverage=(uint64_t *)dlsym(library,"pong_test_sync_coverage");assert(calls && phase && result && coverage);
 NBBuffer *a=[NBBuffer new],*b=[NBBuffer new];
 a.handle=new NmbBuffer{nullptr,nullptr,nullptr,4096};a.wrapped=(void *)0x100000000ull;
 b.handle=new NmbBuffer{nullptr,nullptr,nullptr,4096};b.wrapped=(void *)0x100010000ull;
 IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)@{(id)kIOSurfaceWidth:@4,(id)kIOSurfaceHeight:@4,
  (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@16,(id)kIOSurfaceAllocSize:@4096});assert(surface);
 NBTexture *ta=texture(),*tb=texture();ta.buffer=a;tb.buffer=b;
 ta.surface=(IOSurfaceRef)CFRetain(surface);tb.surface=(IOSurfaceRef)CFRetain(surface);
 assert([ta buffer]==nil && [tb buffer]==nil); // public Metal API must remain correct
 NBTexture *view=(NBTexture *)[ta newTextureViewWithPixelFormat:MTLPixelFormatBGRA8Unorm];
 assert([view buffer]==nil && [view hostImportBacking]==a);
 NSMutableSet *owners=[NSMutableSet setWithObjects:ta,tb,view,nil];HostRanges covered;
 *coverage=1;
 assert(syncActiveImports([NSMutableSet set],owners,2,covered));
 assert(*calls==1 && *phase==2 && covered.size()==1 && covered[0].first==0x100000000ull);
 covered.clear();*coverage=3;
 assert(syncActiveImports([NSMutableSet set],owners,1,covered) && covered.size()==2 && *phase==1);
 covered.clear();*result=(int32_t)0xe00002c7u;
 assert(syncActiveImports([NSMutableSet set],owners,2,covered) && covered.empty()); // older ABI keeps maintenance
 *result=(int32_t)0xe00002d8u;
 assert(!syncActiveImports([NSMutableSet set],owners,2,covered) && covered.empty()); // real failure refuses command
 CFRelease(surface);
 gLoaderPath.clear(); // cache keeps function pointers; mock remains loaded for process lifetime
 puts("PASS actual IOSurface/view backing reaches sync exports while public buffer stays nil; exact coverage, phase2/1 and conservative errors");
}
int main() {@autoreleasepool {
 assert(!PongMetalDeviceAbiAvailable(Nil));
 setenv("NATIVE_METAL_SINGLE_SAMPLE","1",1);gInFlight=[NSMutableSet set];
 lateResolve();noPrematureResolve();knownActions();aspectsAndOptions();bounds();presentation();failedHandoff();surfaceAndSpan();libraryAndPressure();hostSyncCoverage();
 puts("PASS frontend regression suite (recording runtime, no GPU)");
}}
