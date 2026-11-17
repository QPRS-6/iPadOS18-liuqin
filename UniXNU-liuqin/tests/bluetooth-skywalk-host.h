/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Recording kernel API model for the actual generated UART.SK C source. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef int errno_t,boolean_t,nexus_port_t;
typedef void *proc_t,*kern_ctl_ref,*kern_nexus_provider_t,*kern_nexus_t,*kern_channel_t,*nexus_controller_t,*nexus_attr_t;
typedef uint8_t uuid_t[16];
typedef pthread_mutex_t lck_mtx_t;
typedef struct {int value;} lck_grp_t;
#define LCK_GRP_ATTR_NULL NULL
#define LCK_ATTR_NULL NULL
#define PZERO 0
#define PCATCH 1
#define CTL_FLAG_REG_SOCK_STREAM 1
#define NEXUS_TYPE_KERNEL_PIPE 1
#define NEXUS_ATTR_SLOT_BUF_SIZE 1
#define NEXUS_ATTR_TX_SLOTS 2
#define NEXUS_ATTR_RX_SLOTS 3
#define KERN_NEXUS_DOMAIN_PROVIDER_CURRENT_VERSION 1
#define NXPIF_VIRTUAL_DEVICE 1
#define KERN_PBUFPOOL_CURRENT_VERSION 1
#define KBIF_VIRTUAL_DEVICE 1
#define KBIF_QUANTUM 2
#define KERN_NEXUS_CURRENT_VERSION 1
#define NEXUS_PORT_KERNEL_PIPE_CLIENT 1
#define NEXUS_BIND_PID 1
/* Match the exported Darwin24 storage-only macro behavior. Name lookup must
 * fail until the consumer explicitly registers these real handler records. */
#define CTLTYPE_INT 2
#define CTLTYPE_STRING 3
#define CTLFLAG_RD 0x80000000u
#define CTLFLAG_LOCKED 0x00800000u
#define OID_AUTO (-1)
struct sysctl_oid;
struct sysctl_req {void *newptr,*oldptr;size_t size;};
#define SYSCTL_HANDLER_ARGS (struct sysctl_oid *oidp,void *arg1,int arg2,struct sysctl_req *req)
struct sysctl_oid {const char *name;unsigned flags;void *arg1;int arg2;int (*handler) SYSCTL_HANDLER_ARGS;};
static int sysctl_handle_string SYSCTL_HANDLER_ARGS;
#define SYSCTL_PROC(parent,nbr,name,flags,ptr,arg,handler,fmt,desc) \
 struct sysctl_oid sysctl_##parent##_##name={#name,(flags),(ptr),(arg),(handler)}
#define SYSCTL_STRING(parent,nbr,name,flags,ptr,len,desc) \
 struct sysctl_oid sysctl_##parent##_##name={#name,CTLTYPE_STRING|(flags),(ptr),(len),sysctl_handle_string}
static struct sysctl_oid *registered_oids[4];
static unsigned registered_oid_count;
void sysctl_register_oid(struct sysctl_oid *oid) {
 assert(oid&&oid->name&&oid->handler&&(oid->flags&CTLFLAG_RD));
 assert(registered_oid_count<4);
 for(unsigned i=0;i<registered_oid_count;i++)assert(strcmp(registered_oids[i]->name,oid->name));
 registered_oids[registered_oid_count++]=oid;
}
int pong_bt_test_sysctl_read(const char *name,void *result,size_t bytes) {
 assert(!strncmp(name,"kern.",5));
 for(unsigned i=0;i<registered_oid_count;i++) {
  struct sysctl_oid *oid=registered_oids[i];if(strcmp(name+5,oid->name))continue;
  struct sysctl_req request={NULL,result,bytes};return oid->handler(oid,oid->arg1,oid->arg2,&request);
 }
 return ENOENT;
}
static int sysctl_out(struct sysctl_req *r,const void *p,size_t n){if(r->oldptr){if(r->size<n)return ENOMEM;memcpy(r->oldptr,p,n);}return 0;}
#define SYSCTL_OUT(r,p,n) sysctl_out(r,p,n)
static int sysctl_handle_string SYSCTL_HANDLER_ARGS {
 (void)oidp;(void)arg2;assert(arg1);return SYSCTL_OUT(req,arg1,strlen(arg1)+1);
}
static bool authorized=true;
static int current_pid=65,register_error,allocation_error;
static unsigned wake_calls,power_calls,notifications,destroyed,bind_pid;
static void (*sleep_hook)(void);
int pong_bluetooth_native_client_allowed(void){return authorized;}
static int proc_selfpid(void){return current_pid;}
static lck_grp_t *lck_grp_alloc_init(const char *n __unused,void *a __unused){return allocation_error?NULL:calloc(1,sizeof(lck_grp_t));}
static void lck_grp_free(lck_grp_t *p){free(p);}
static lck_mtx_t *lck_mtx_alloc_init(lck_grp_t *g __unused,void *a __unused){lck_mtx_t *p=malloc(sizeof(*p));if(p)pthread_mutex_init(p,NULL);return p;}
static void lck_mtx_lock(lck_mtx_t *p){assert(p);assert(!pthread_mutex_lock(p));}
static void lck_mtx_unlock(lck_mtx_t *p){assert(!pthread_mutex_unlock(p));}
static void wakeup(void *p __unused){}
static int msleep(void *p __unused,lck_mtx_t *m,int pri __unused,const char *n __unused,struct timespec *t __unused){lck_mtx_unlock(m);int e=0;if(sleep_hook){void (*h)(void)=sleep_hook;sleep_hook=NULL;h();}else e=ETIMEDOUT;lck_mtx_lock(m);return e;}
static size_t fixture_strlcpy(char *d,const char *s,size_t n){size_t z=strlen(s);if(n){size_t k=z<n-1?z:n-1;memcpy(d,s,k);d[k]=0;}return z;}
#define strlcpy fixture_strlcpy
static void uuid_clear(uuid_t u){memset(u,0,16);}
static void uuid_copy(uuid_t d,const uuid_t s){memcpy(d,s,16);}
static bool uuid_is_null(const uuid_t u){uint8_t n=0;for(unsigned i=0;i<16;i++)n|=u[i];return !n;}
struct sockaddr_ctl {uint32_t sc_unit;};
typedef void *mbuf_t;
static void mbuf_freem(mbuf_t p){free(p);}
struct kern_ctl_reg {
 uint32_t ctl_flags;char ctl_name[96];
 errno_t (*ctl_connect)(kern_ctl_ref,struct sockaddr_ctl *,void **);
 errno_t (*ctl_disconnect)(kern_ctl_ref,uint32_t,void *);
 errno_t (*ctl_send)(kern_ctl_ref,uint32_t,void *,mbuf_t,int);
 errno_t (*ctl_setopt)(kern_ctl_ref,uint32_t,void *,int,void *,size_t);
 errno_t (*ctl_getopt)(kern_ctl_ref,uint32_t,void *,int,void *,size_t *);
};
static struct kern_ctl_reg control;
static int ctl_register(struct kern_ctl_reg *r,kern_ctl_ref *ref){if(register_error)return register_error;control=*r;*ref=&control;return 0;}
struct mock_packet {uint8_t bytes[4096];uint32_t offset,length,limit;};
typedef struct mock_packet *kern_packet_t,*kern_buflet_t;
struct mock_slot {kern_packet_t packet;unsigned index;};
struct mock_ring {struct mock_slot slots[4];unsigned count,advanced;};
typedef struct mock_slot *kern_channel_slot_t;
typedef struct mock_ring *kern_channel_ring_t;
typedef void *kern_pbufpool_t;
struct kern_nexus_provider_init {
 unsigned nxpi_version,nxpi_flags;
 errno_t (*nxpi_pre_connect)(kern_nexus_provider_t,proc_t,kern_nexus_t,nexus_port_t,kern_channel_t,void **);
 errno_t (*nxpi_connected)(kern_nexus_provider_t,kern_nexus_t,kern_channel_t);
 void (*nxpi_pre_disconnect)(kern_nexus_provider_t,kern_nexus_t,kern_channel_t);
 void (*nxpi_disconnected)(kern_nexus_provider_t,kern_nexus_t,kern_channel_t);
 errno_t (*nxpi_ring_init)(kern_nexus_provider_t,kern_nexus_t,kern_channel_t,kern_channel_ring_t,boolean_t,void **);
 void (*nxpi_ring_fini)(kern_nexus_provider_t,kern_nexus_t,kern_channel_ring_t);
 errno_t (*nxpi_sync_tx)(kern_nexus_provider_t,kern_nexus_t,kern_channel_ring_t,uint32_t);
 errno_t (*nxpi_sync_rx)(kern_nexus_provider_t,kern_nexus_t,kern_channel_ring_t,uint32_t);
};
static struct kern_nexus_provider_init provider_ops;
struct kern_pbufpool_init {unsigned kbi_version,kbi_flags,kbi_packets,kbi_bufsize,kbi_max_frags;char kbi_name[96];};
struct kern_nexus_init {unsigned nxi_version;kern_pbufpool_t nxi_tx_pbufpool;};
static int kern_nexus_controller_create(nexus_controller_t *n){*n=malloc(1);return *n?0:ENOMEM;}
static int kern_nexus_get_default_domain_provider(unsigned t __unused,uuid_t *u){uuid_clear(*u);(*u)[0]=1;return 0;}
static int kern_nexus_attr_create(nexus_attr_t *a){*a=malloc(1);return *a?0:ENOMEM;}
static int kern_nexus_attr_set(nexus_attr_t a __unused,unsigned k __unused,unsigned v __unused){return 0;}
static void kern_nexus_attr_destroy(nexus_attr_t a){free(a);}
static int kern_nexus_controller_register_provider(nexus_controller_t n __unused,const uuid_t d __unused,const uint8_t *name,struct kern_nexus_provider_init *i,size_t z,nexus_attr_t a __unused,uuid_t *u){assert(!strcmp((const char *)name,"com.apple.uart.sk.bluetooth")&&z==sizeof(*i));provider_ops=*i;uuid_clear(*u);(*u)[0]=2;return 0;}
static int kern_pbufpool_create(struct kern_pbufpool_init *i,kern_pbufpool_t *p,void *a __unused){assert(i->kbi_bufsize<=4096);*p=malloc(1);return *p?0:ENOMEM;}
static int kern_nexus_controller_alloc_provider_instance(nexus_controller_t n __unused,const uuid_t p __unused,void *a __unused,void *b __unused,uuid_t *u,struct kern_nexus_init *i __unused){uuid_clear(*u);(*u)[0]=3;return 0;}
static int kern_nexus_controller_bind_provider_instance(nexus_controller_t n __unused,const uuid_t u __unused,nexus_port_t *p,int pid,void *a __unused,void *b __unused,unsigned f __unused,unsigned flags){assert(*p==1&&flags==NEXUS_BIND_PID);bind_pid=(unsigned)pid;return 0;}
static int kern_nexus_controller_free_provider_instance(nexus_controller_t n __unused,const uuid_t u __unused){return 0;}
static int kern_nexus_controller_deregister_provider(nexus_controller_t n __unused,const uuid_t u __unused){return 0;}
static void kern_nexus_controller_destroy(nexus_controller_t n){free(n);destroyed++;}
static void kern_pbufpool_destroy(kern_pbufpool_t p){free(p);}
static int kern_channel_notify(kern_channel_ring_t r __unused,unsigned f __unused){notifications++;return 0;}
static kern_channel_slot_t kern_channel_get_next_slot(kern_channel_ring_t r,kern_channel_slot_t prev,void *p __unused){unsigned at=prev?prev->index+1:r->advanced;return at<r->count?&r->slots[at]:NULL;}
static kern_packet_t kern_channel_slot_get_packet(kern_channel_ring_t r __unused,kern_channel_slot_t s){return s->packet;}
static kern_buflet_t kern_packet_get_next_buflet(kern_packet_t p,void *b __unused){return p;}
static uint32_t kern_packet_get_data_length(kern_packet_t p){return p->length;}
static uint32_t kern_buflet_get_data_offset(kern_buflet_t b){return b->offset;}
static uint32_t kern_buflet_get_data_limit(kern_buflet_t b){return b->limit;}
static void *kern_buflet_get_data_address(kern_buflet_t b){return b->bytes;}
static void kern_channel_advance_slot(kern_channel_ring_t r,kern_channel_slot_t p){r->advanced=p->index+1;}
static int kern_channel_reclaim(kern_channel_ring_t r __unused){return 0;}
static int kern_nexus_get_pbufpool(kern_nexus_t n __unused,void *t __unused,kern_pbufpool_t *p){*p=(void *)1;return 0;}
static int kern_pbufpool_alloc_nosleep(kern_pbufpool_t p __unused,unsigned f __unused,kern_packet_t *q){*q=calloc(1,sizeof(**q));if(*q)(*q)->limit=4096;return *q?0:ENOMEM;}
static void kern_pbufpool_free(kern_pbufpool_t p __unused,kern_packet_t q){free(q);}
static void kern_packet_clear_flow_uuid(kern_packet_t p __unused){}
static int kern_buflet_set_data_offset(kern_buflet_t b,unsigned o){b->offset=o;return 0;}
static int kern_buflet_set_data_length(kern_buflet_t b,unsigned n){b->length=n;return 0;}
static int kern_packet_finalize(kern_packet_t p __unused){return 0;}
static int kern_channel_slot_attach_packet(kern_channel_ring_t r __unused,kern_channel_slot_t s,kern_packet_t p){s->packet=p;return 0;}
