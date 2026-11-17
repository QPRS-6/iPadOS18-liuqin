/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Recording VFS for host failure tests only. Never a target ABI substitute. */
#ifndef PONG_BLUETOOTH_RUNTIME_HOST_H
#define PONG_BLUETOOTH_RUNTIME_HOST_H
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#define MAXPATHLEN 1024
#define NULLVP NULL
#define VDIR 1
#define VREG 2
#define VLNK 3
#define VNODE_LOOKUP_NOFOLLOW 1
#define FREAD 1
#define FWRITE 2
#define O_CREAT 0x200
#define O_EXCL 0x800
#define O_NOFOLLOW 0x100
#define IO_NOAUTH 1
#define IO_UNIT 2
#define MNT_WAIT 1
#define CREATE 1
#define OP_MKDIR 2
#define LOCKPARENT 8
#define UIO_SYSSPACE 1
#define UIO_READ 0
#define UIO_WRITE 1
#ifndef CAST_USER_ADDR_T
#define CAST_USER_ADDR_T(p) ((uintptr_t)(p))
#endif
struct vnode_attr {uint64_t va_supported,va_active,va_data_size,va_nlink;unsigned va_uid,va_gid,va_mode,va_type;};
enum {VNODE_ATTR_va_uid=1,VNODE_ATTR_va_gid=2,VNODE_ATTR_va_mode=4,VNODE_ATTR_va_data_size=8,VNODE_ATTR_va_nlink=16,VNODE_ATTR_va_type=32};
#define VATTR_INIT(a) memset((a),0,sizeof(*(a)))
#define VATTR_WANTED(v,a) ((v)->va_active|=VNODE_ATTR_##a)
#define VATTR_SET(v,a,x) do {VATTR_WANTED(v,a);(v)->a=(x);} while(0)
#define VATTR_IS_SUPPORTED(v,a) ((v)->va_supported&VNODE_ATTR_##a)
struct test_vnode {bool present;unsigned type;struct vnode_attr attrs;unsigned char data[4096];int refs,opens;char path[MAXPATHLEN],display_path[MAXPATHLEN];};
typedef struct test_vnode *vnode_t;
struct test_proc {int pid;unsigned uid;vnode_t executable;};
typedef struct test_proc *proc_t;
struct test_context {proc_t process;unsigned uid;};
typedef struct test_context *vfs_context_t;
typedef unsigned kauth_cred_t;
struct componentname {vfs_context_t cn_context;};
struct nameidata {uintptr_t ni_dirp;vnode_t ni_vp,ni_dvp;struct componentname ni_cnd;};
#define NDINIT(nd,op,pop,flags,seg,name,ctx) do { \
    assert((op)==CREATE&&(pop)==OP_MKDIR&&(flags)==LOCKPARENT&&(seg)==UIO_SYSSPACE); \
    (nd)->ni_dirp=(name);(nd)->ni_cnd.cn_context=(ctx); \
} while(0)
int vnode_lookup(const char *,int,vnode_t *,vfs_context_t);
int vnode_getattr(vnode_t,struct vnode_attr *,vfs_context_t);
int vnode_isdir(vnode_t);
int vnode_isreg(vnode_t);
int vnode_put(vnode_t);
int namei(struct nameidata *);
void nameidone(struct nameidata *);
int VNOP_MKDIR(vnode_t,vnode_t *,struct componentname *,struct vnode_attr *,vfs_context_t);
int vnode_open(const char *,int,int,int,vnode_t *,vfs_context_t);
int vnode_close(vnode_t,int,vfs_context_t);
int vn_rdwr(int,vnode_t,caddr_t,int,uint64_t,int,int,kauth_cred_t,int *,proc_t);
int VNOP_FSYNC(vnode_t,int,vfs_context_t);
proc_t vfs_context_proc(vfs_context_t);
kauth_cred_t vfs_context_ucred(vfs_context_t);
unsigned kauth_cred_getuid(kauth_cred_t);
int proc_pid(proc_t);
vfs_context_t vfs_context_current(void);
vnode_t proc_getexecutablevnode(proc_t);
int vn_getpath(vnode_t,char *,int *);
#endif
