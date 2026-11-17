/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Versioned owner-built runtime in Data; original OS executables stay untouched. */
#ifdef PONG_BLUETOOTH_RUNTIME_TEST
#include "bluetooth-runtime-host.h"
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/kauth.h>
#endif
#include "PongBluetoothRuntimeData.h"
static volatile uint32_t runtime_stage;
static int runtime_error;
static const char root[]="/private/var/tmp/pong-bt97";
static const char tool[]="/private/var/tmp/pong-bt97/BlueTool";
static const char daemon[]="/private/var/tmp/pong-bt97/bluetoothd";
static uint32_t redirected[2],auth_traces;
static bool trace_slot(uint32_t *counter,uint32_t limit) {
    uint32_t value=__atomic_load_n(counter,__ATOMIC_RELAXED);
    for(unsigned attempt=0;attempt<8 && value<limit;attempt++)
        if(__atomic_compare_exchange_n(counter,&value,value+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return true;
    return false;
}
int pong_bt_runtime_ready(void){return __atomic_load_n(&runtime_stage,__ATOMIC_ACQUIRE)==3;}

#ifndef PONG_BLUETOOTH_RUNTIME_TEST
/* Owner93: SIGSEGV has no fault PC in the available log. Record the fault site
 * only for the installed daemon, bounded, with no signal/exception changes. */
static uint32_t user_fault_traces;
void pong_bt_user_fault(uint64_t pc,uint64_t lr,uint64_t far,uint64_t esr,unsigned code,unsigned type,uint64_t r0,uint64_t r1,uint64_t r8,uint64_t r9) {
    if(!pong_bt_runtime_ready())return;
    proc_t p=current_proc();if(!p)return;
    const char *name=proc_best_name(p);
    if(!name || strcmp(name,"bluetoothd"))return;
    uint32_t value=__atomic_load_n(&user_fault_traces,__ATOMIC_RELAXED);
    for(unsigned attempt=0;attempt<4 && value<8;attempt++)
        if(__atomic_compare_exchange_n(&user_fault_traces,&value,value+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {
            printf("Pong Bluetooth97: bluetoothd user fault pc=%llx lr=%llx far=%llx esr=%llx code=%u type=%u x0=%llx x1=%llx x8=%llx x9=%llx; bounded\n",
                pc,lr,far,esr,code,type,r0,r1,r8,r9);
            return;
        }
}
/* Owner116: bounded record of HID user-client opens so a missing system HID
 * device (IOHIDUserDevice) can be attributed to a provider or a denial. */
static uint32_t user_client_traces;
void pong_bt_user_client_trace(const char *provider,unsigned type,int result,int pid) {
    uint32_t value=__atomic_load_n(&user_client_traces,__ATOMIC_RELAXED);
    for(unsigned attempt=0;attempt<4 && value<32;attempt++)
        if(__atomic_compare_exchange_n(&user_client_traces,&value,value+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {
            printf("Pong Bluetooth116: user client provider=%s type=%u result=%x pid=%d\n",
                provider?provider:"?",type,(unsigned)result,pid);
            return;
        }
}
/* Owner110: a clean exit(1) is invisible to ReportCrash. Record the bounded
 * user pc/lr of the installed daemon only; no signal or behavior change. */
static uint32_t exit_traces;
void pong_bt_exit_trace(const char *name,int rv,uint64_t pc,uint64_t lr,uint64_t x19,uint64_t x20,uint64_t sp) {
    if(!pong_bt_runtime_ready())return;
    if(rv==0 || !name || strcmp(name,"bluetoothd"))return;
    (void)sp;
    uint32_t value=__atomic_load_n(&exit_traces,__ATOMIC_RELAXED);
    for(unsigned attempt=0;attempt<4 && value<6;attempt++)
        if(__atomic_compare_exchange_n(&exit_traces,&value,value+1,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {
            /* Owner111: the daemon restart helper prints reason/context to
             * os_log, which this port cannot capture. x20 carries the numeric
             * reason enum; the matching context string is read from the
             * installed image on the host. No user memory is read here. */
            /* Bounded prefix of the daemon's own context string; the restart
             * helper uses %%s with a C string. Never read beyond 48 bytes and
             * stop at NUL; non-printable bytes become dots. */
            char text[49],raw[48];unsigned have=0;
            if(x19>=0x100000000ull) {
                if(copyin((uint64_t)x19,raw,sizeof(raw))==0) {
                    for(unsigned i=0;i<sizeof(raw) && have<sizeof(text)-1;i++) {
                        if(raw[i]==0)break;
                        text[have++]=(raw[i]>=0x20 && raw[i]<0x7f)?raw[i]:'.';
                    }
                }
            }
            text[have]=0;
            printf("Pong Bluetooth112: bluetoothd exit rv=%d pc=%llx lr=%llx reason=%llu context=\"%s\"; bounded\n",
                rv,pc,lr,(unsigned long long)(x20&0xffffffffull),text);
            return;
        }
}
#endif
static int directory_valid(vnode_t vp,vfs_context_t ctx) {
    struct vnode_attr a;VATTR_INIT(&a);VATTR_WANTED(&a,va_uid);VATTR_WANTED(&a,va_gid);VATTR_WANTED(&a,va_mode);
    int e=vnode_getattr(vp,&a,ctx);if(e)return e;
    return vnode_isdir(vp) && VATTR_IS_SUPPORTED(&a,va_uid) && VATTR_IS_SUPPORTED(&a,va_gid) &&
        VATTR_IS_SUPPORTED(&a,va_mode) && a.va_uid==0 && a.va_gid==0 && (a.va_mode&07777)==0711?0:EPERM;
}
static int directory(vfs_context_t ctx) {
    vnode_t vp=NULLVP;int e=vnode_lookup(root,VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
    if(!e){e=directory_valid(vp,ctx);vnode_put(vp);return e;}
    if(e!=ENOENT)return e;
    struct nameidata nd={0};NDINIT(&nd,CREATE,OP_MKDIR,LOCKPARENT,UIO_SYSSPACE,CAST_USER_ADDR_T(root),ctx);
    e=namei(&nd);if(e)return e;
    if(nd.ni_vp){vnode_put(nd.ni_vp);e=EEXIST;}
    else {
        /* Both original22H374 jobs run as mobile. Permit traversal, not listing
         * or writes; do not elevate the jobs or alter their launchd definitions. */
        struct vnode_attr a;VATTR_INIT(&a);VATTR_SET(&a,va_type,VDIR);VATTR_SET(&a,va_mode,0711);VATTR_SET(&a,va_uid,0);VATTR_SET(&a,va_gid,0);
        e=VNOP_MKDIR(nd.ni_dvp,&vp,&nd.ni_cnd,&a,ctx);
        if(!e)e=vp?directory_valid(vp,ctx):EIO;
        if(vp)vnode_put(vp);
    }
    nameidone(&nd);if(nd.ni_dvp)vnode_put(nd.ni_dvp);return e;
}
static int equal_file(vnode_t vp,const unsigned char *bytes,unsigned size,vfs_context_t ctx) {
    struct vnode_attr a;VATTR_INIT(&a);VATTR_WANTED(&a,va_uid);VATTR_WANTED(&a,va_gid);VATTR_WANTED(&a,va_mode);VATTR_WANTED(&a,va_data_size);VATTR_WANTED(&a,va_nlink);
    int error=vnode_getattr(vp,&a,ctx);if(error)return error;
    if(!vnode_isreg(vp) || !VATTR_IS_SUPPORTED(&a,va_uid) || !VATTR_IS_SUPPORTED(&a,va_gid) ||
       !VATTR_IS_SUPPORTED(&a,va_mode) || !VATTR_IS_SUPPORTED(&a,va_data_size) || !VATTR_IS_SUPPORTED(&a,va_nlink) ||
       a.va_uid!=0 || a.va_gid!=0 || (a.va_mode&07777)!=0555 || a.va_data_size!=size || a.va_nlink!=1)return EEXIST;
    unsigned char buffer[1024];
    for(unsigned at=0;at<size;at+=sizeof(buffer)){unsigned n=size-at<sizeof(buffer)?size-at:(unsigned)sizeof(buffer);int resid=0;
        int e=vn_rdwr(UIO_READ,vp,(caddr_t)buffer,(int)n,at,UIO_SYSSPACE,IO_NOAUTH,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
        if(e||resid||memcmp(buffer,bytes+at,n))return e?e:EEXIST;}
    return 0;
}
static int install_file(const char *path,const unsigned char *bytes,unsigned size,vfs_context_t ctx) {
    vnode_t vp=NULLVP;int e=vnode_open(path,FREAD|O_NOFOLLOW,0,0,&vp,ctx);
    if(!e){e=equal_file(vp,bytes,size,ctx);int close=vnode_close(vp,FREAD,ctx);return e?e:close;}
    if(e!=ENOENT)return e;
    e=vnode_open(path,O_CREAT|O_EXCL|FWRITE|O_NOFOLLOW,0555,0,&vp,ctx);if(e)return e;
    int resid=0;e=vn_rdwr(UIO_WRITE,vp,(caddr_t)(uintptr_t)bytes,(int)size,0,UIO_SYSSPACE,IO_NOAUTH|IO_UNIT,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
    if(!e && resid)e=EIO;if(!e)e=VNOP_FSYNC(vp,MNT_WAIT,ctx);int close=vnode_close(vp,FWRITE,ctx);
    if(e || close)return e?e:close;
    /* Verify newly created files too; a partial/different file is never activated. */
    e=vnode_open(path,FREAD|O_NOFOLLOW,0,0,&vp,ctx);if(e)return e;
    e=equal_file(vp,bytes,size,ctx);close=vnode_close(vp,FREAD,ctx);return e?e:close;
}
const char *pong_bt_runtime_exec(const char *path,vfs_context_t ctx) {
    if(!path)return path;
    if(ctx && !strcmp(path,"/sbin/launchd.ios") && proc_pid(vfs_context_proc(ctx))==1) {
        uint32_t expected=0;
        if(__atomic_compare_exchange_n(&runtime_stage,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
            int e=kauth_cred_getuid(vfs_context_ucred(ctx))==0?directory(ctx):EPERM;
            if(!e)e=install_file(tool,pong_bt_bluetool,pong_bt_bluetool_size,ctx);
            if(!e)e=install_file(daemon,pong_bt_daemon,pong_bt_daemon_size,ctx);
            runtime_error=e;__atomic_store_n(&runtime_stage,e?2:3,__ATOMIC_RELEASE);
            printf("Pong Bluetooth97: matching22H374 runtime version=pong-bt97 install result=%d ready=%u; OS files/Data seed/GPU unchanged\n",runtime_error,pong_bt_runtime_ready()?1:0);
        }
    }
    if(pong_bt_runtime_ready()) {
        if(!strcmp(path,"/usr/sbin/BlueTool")){
            if(trace_slot(&redirected[0],4))printf("Pong Bluetooth82: exec redirect BlueTool pid=%d; verified installed runtime path\n",ctx?proc_pid(vfs_context_proc(ctx)):-1);
            return tool;
        }
        if(!strcmp(path,"/usr/sbin/bluetoothd")){
            if(trace_slot(&redirected[1],4))printf("Pong Bluetooth82: exec redirect bluetoothd pid=%d; verified installed runtime path\n",ctx?proc_pid(vfs_context_proc(ctx)):-1);
            return daemon;
        }
    }
    return path;
}
static int installed_image(vnode_t executable,const char *path,const unsigned char *bytes,unsigned size) {
    vnode_t installed=NULLVP;vfs_context_t ctx=vfs_context_current();
    int e=vnode_lookup(path,VNODE_LOOKUP_NOFOLLOW,&installed,ctx);if(e)return e;
    /* APFS/firmlink display paths need not equal the requested spelling. Bind
     * the actual executable vnode to the canonical installed path, then verify
     * its real metadata and all bytes. Identical bytes at another path fail. */
    e=installed==executable?equal_file(executable,bytes,size,ctx):EPERM;
    vnode_put(installed);return e;
}
int pong_bt_runtime_client_allowed(proc_t p) {
    if(!pong_bt_runtime_ready()||!p)return 0;
    vnode_t vp=proc_getexecutablevnode(p);if(!vp)return 0;
    int tool_error=installed_image(vp,tool,pong_bt_bluetool,pong_bt_bluetool_size);
    int daemon_error=tool_error?installed_image(vp,daemon,pong_bt_daemon,pong_bt_daemon_size):EPERM;
    int allowed=!tool_error||!daemon_error;
    if(trace_slot(&auth_traces,16))
        printf("Pong Bluetooth82: runtime auth pid=%d role=%s toolResult=%d daemonResult=%d allowed=%d; executable values/addresses not logged\n",proc_pid(p),!tool_error?"BlueTool":!daemon_error?"bluetoothd":"unqualified",tool_error,daemon_error,allowed);
    vnode_put(vp);return allowed;
}
