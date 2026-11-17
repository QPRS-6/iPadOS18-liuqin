/* SPDX-License-Identifier: MIT */
/* Opt-in Freeform incident capture for a guest without a working ReportCrash
 * backend. Uses stderr/applog, independent of pstore. It never resumes a fatal
 * signal or replaces an application's existing signal handler. */
#pragma once
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <initializer_list>

namespace pongmetaldiag {
static char identity[128];
static NSUncaughtExceptionHandler *previousException;
static volatile sig_atomic_t entered;

// No Foundation, stdio, allocator, dynamic loader or unwinder in the signal
// handler. Frame records are copied with a bounded Mach read, never dereferenced.
struct Line {
    char bytes[512];size_t used=0;
    void text(const char *s) {while(*s && used+1<sizeof bytes)bytes[used++]=*s++;}
    void number(uint64_t n,unsigned radix=16) {
        char digits[32];unsigned count=0;
        do {digits[count++]="0123456789abcdef"[n%radix];n/=radix;} while(n && count<sizeof digits);
        while(count && used+1<sizeof bytes)bytes[used++]=digits[--count];
    }
    void send() {bytes[used++]='\n';(void)write(STDERR_FILENO,bytes,used);}
};

static void fatal(int number,siginfo_t *info,void *saved) {
    if(!__atomic_exchange_n(&entered,1,__ATOMIC_RELAXED)) {
        uint64_t pc=0,lr=0,fp=0,sp=0;
        const auto *context=static_cast<const ucontext_t *>(saved);
        if(context && context->uc_mcontext) {
#if defined(__aarch64__)
            const auto &state=context->uc_mcontext->__ss;
            pc=state.__pc;lr=state.__lr;fp=state.__fp;sp=state.__sp;
#elif defined(__x86_64__)
            const auto &state=context->uc_mcontext->__ss;
            pc=state.__rip;fp=state.__rbp;sp=state.__rsp;
#endif
        }
        Line line;line.text(identity);line.text("fatal signal=");line.number((unsigned)number,10);
        line.text(" code=");line.number(info?(uint32_t)info->si_code:0);
        line.text(" address=");line.number(info?(uintptr_t)info->si_addr:0);
        line.text(" pc=");line.number(pc);line.text(" lr=");line.number(lr);
        line.text(" fp=");line.number(fp);line.text(" sp=");line.number(sp);line.send();
        for(unsigned i=0;i<16 && fp>=sp && fp-sp<(16u<<20) && !(fp&7);i++) {
            uint64_t frame[2]={};vm_size_t copied=0;
            if(vm_read_overwrite(mach_task_self(),(vm_address_t)fp,sizeof frame,(vm_address_t)frame,&copied)!=KERN_SUCCESS ||
               copied!=sizeof frame)break;
            Line item;item.text(identity);item.text("frame=");item.number(i,10);
            item.text(" pc=");item.number(frame[1]);item.send();
            if(frame[0]<=fp || frame[0]-fp>(1u<<20))break;
            fp=frame[0];
        }
    }
    // SA_RESETHAND has already restored the default disposition. The signal
    // is queued while blocked and delivered when this handler returns.
    (void)raise(number);
}

static void exception(NSException *value) {
    char line[896];
    int n=snprintf(line,sizeof line,"%suncaught name=%.120s reason=%.600s\n",identity,
        value.name.UTF8String?:"?",value.reason.UTF8String?:"?");
    if(n>0)(void)write(STDERR_FILENO,line,(size_t)n<sizeof line?(size_t)n:sizeof(line)-1);
    NSArray<NSString *> *frames=value.callStackSymbols;
    for(NSUInteger i=0;i<frames.count && i<16;i++) {
        n=snprintf(line,sizeof line,"%sexception-frame=%lu %.700s\n",identity,(unsigned long)i,frames[i].UTF8String?:"?");
        if(n>0)(void)write(STDERR_FILENO,line,(size_t)n<sizeof line?(size_t)n:sizeof(line)-1);
    }
    if(previousException)previousException(value);
}

static bool requested(const char *args) {
    const char *key="pong_gpu_appdiag=1";const size_t length=strlen(key);
    for(const char *p=args;(p=strstr(p,key));p+=length)
        if((p==args || p[-1]==' ') && (!p[length] || p[length]==' '))return true;
    return false;
}

static void install(const char *args) {
    // Process selection limits diagnostics only; no renderer or shader answer
    // is keyed to an application. Normal boots install no fatal-signal hooks.
    if(!requested(args) || !getprogname() || strcmp(getprogname(),"Freeform"))return;
    static dispatch_once_t once;
    dispatch_once(&once,^{
        snprintf(identity,sizeof identity,"Pong Metal app: pid=%d process=Freeform ",getpid());
        unsigned mask=0;
        for(int number:{SIGABRT,SIGSEGV,SIGBUS,SIGILL,SIGTRAP}) {
            struct sigaction old={},action={};
            if(sigaction(number,nullptr,&old) || old.sa_handler!=SIG_DFL)continue;
            action.sa_sigaction=fatal;action.sa_flags=SA_SIGINFO|SA_RESETHAND;
            sigemptyset(&action.sa_mask);
            if(!sigaction(number,&action,nullptr))mask|=1u<<number;
        }
        previousException=NSGetUncaughtExceptionHandler();NSSetUncaughtExceptionHandler(exception);
        Line line;line.text(identity);line.text("diagnostics installed signals=");line.number(mask);line.send();
        // Capture image bases outside the fatal handler for offline symbolication.
        unsigned emitted=0;
        for(uint32_t i=0,n=_dyld_image_count();i<n && emitted<20;i++) {
            const char *name=_dyld_get_image_name(i);if(!name)continue;
            bool relevant=false;
            for(const char *part:{"/Freeform","/PencilKit","/RenderBox","/Metal.framework/","/UIKitCore",
                "/QuartzCore","/CoreGraphics","/Foundation.framework/","/libobjc.","/PongMetalBridge","/PongAdrenoMetal","/libvulkan_freedreno"})
                if(strstr(name,part)){relevant=true;break;}
            if(!relevant)continue;
            Line image;image.text(identity);image.text("image base=");image.number((uintptr_t)_dyld_get_image_header(i));
            image.text(" slide=");image.number((uint64_t)_dyld_get_image_vmaddr_slide(i));
            image.text(" path=");image.text(name);image.send();emitted++;
        }
    });
}
}
