/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Separate blocking BSD queue for actual native SPI pen reports. */
#include <stdbool.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include "PongNativePenABI.h"

#define PEN_QUEUE_SIZE 512
static lck_grp_t *native_pen_group;
static lck_mtx_t *native_pen_lock;
static unsigned initialized;
static struct pong_native_pen samples[PEN_QUEUE_SIZE];
static uint32_t head,tail;
static bool opened,ready;
static int pen_major=-1;

void pong_native_pen_post(const struct pong_native_pen *sample) {
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=2 || !sample || sample->range>1 || sample->touch>1 || sample->x>65535 || sample->y>65535 ||
       sample->pressure>4095 || (sample->touch && !sample->range))return;
    lck_mtx_lock(native_pen_lock);
    if(tail-head==PEN_QUEUE_SIZE)++head;
    struct pong_native_pen clean=*sample;
    if(!(clean.reserved&PONG_PEN_TILT_VALID) || clean.tiltX<-90 || clean.tiltX>90 || clean.tiltY<-90 || clean.tiltY>90) {
        clean.reserved&=~PONG_PEN_TILT_VALID;clean.tiltX=0;clean.tiltY=0;
    }
    samples[tail++%PEN_QUEUE_SIZE]=clean;
    lck_mtx_unlock(native_pen_lock);wakeup(samples);
}
static int pen_open(__unused dev_t dev,int flags,__unused int kind,__unused struct proc *p) {
    if(flags&FWRITE)return EACCES;
    lck_mtx_lock(native_pen_lock);
    int result=opened?EBUSY:0;
    if(!result){opened=true;head=tail;}
    lck_mtx_unlock(native_pen_lock);return result;
}
static int pen_close(__unused dev_t dev,__unused int flags,__unused int kind,__unused struct proc *p) {
    lck_mtx_lock(native_pen_lock);opened=false;lck_mtx_unlock(native_pen_lock);return 0;
}
static int pen_read(__unused dev_t dev,struct uio *uio,int flags) {
    if(uio_resid(uio)<(user_ssize_t)sizeof(struct pong_native_pen))return EINVAL;
    int error=0;lck_mtx_lock(native_pen_lock);
    while(head==tail) {
        if(flags&IO_NDELAY){error=EWOULDBLOCK;break;}
        error=msleep(samples,native_pen_lock,PZERO|PCATCH,"native-pen",NULL);if(error)break;
    }
    if(error){lck_mtx_unlock(native_pen_lock);return error;}
    struct pong_native_pen sample=samples[head++%PEN_QUEUE_SIZE];
    lck_mtx_unlock(native_pen_lock);return uiomove((caddr_t)&sample,sizeof(sample),uio);
}
static const struct cdevsw native_pen_cdev={
    .d_open=pen_open,.d_close=pen_close,.d_read=pen_read,.d_write=eno_rdwrt,
    .d_ioctl=eno_ioctl,.d_stop=eno_stop,.d_reset=eno_reset,.d_select=eno_select,
    .d_mmap=eno_mmap,.d_strategy=eno_strat,.d_type=0,
};
bool pong_native_pen_device_ready(void) {
    unsigned state=__atomic_load_n(&initialized,__ATOMIC_ACQUIRE);
    if(state==2)return ready;
    unsigned empty=0;
    if(!__atomic_compare_exchange_n(&initialized,&empty,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return false;
    native_pen_group=lck_grp_alloc_init("pong-native-pen",LCK_GRP_ATTR_NULL);
    if(native_pen_group)native_pen_lock=lck_mtx_alloc_init(native_pen_group,LCK_ATTR_NULL);
    if(!native_pen_lock){__atomic_store_n(&initialized,3,__ATOMIC_RELEASE);return false;}
    pen_major=cdevsw_add(-1,&native_pen_cdev);
    if(pen_major>=0 && devfs_make_node(makedev(pen_major,0),DEVFS_CHAR,UID_ROOT,GID_WHEEL,0444,"pong-pen"))ready=true;
    __atomic_store_n(&initialized,ready?2:3,__ATOMIC_RELEASE);return ready;
}
