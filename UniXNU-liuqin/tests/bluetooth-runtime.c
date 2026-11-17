/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define PONG_BLUETOOTH_RUNTIME_TEST 1
#include "../drivers/PongBluetooth/PongBluetoothRuntime.c"
const unsigned char pong_bt_bluetool[2051]={1,3,5,7};
const unsigned char pong_bt_daemon[1539]={2,4,6,8};
const unsigned pong_bt_bluetool_size=sizeof(pong_bt_bluetool),pong_bt_daemon_size=sizeof(pong_bt_daemon);
static struct test_vnode nodes[4];
static struct test_proc process;
static struct test_context context;
static int scenario,writes,fsyncs,creates,finalized;
static void node_init(unsigned index,unsigned type,unsigned mode) {
    struct test_vnode *n=&nodes[index];n->present=true;n->type=type;n->attrs.va_supported=63;
    n->attrs.va_uid=n->attrs.va_gid=0;n->attrs.va_mode=mode;n->attrs.va_nlink=1;
    const char *path=index==0?"/private/var/tmp":index==1?root:index==2?tool:daemon;
    strcpy(n->path,path);
}
static struct test_vnode *find(const char *path) {
    for(unsigned i=0;i<4;i++)if(!strcmp(nodes[i].path,path))return &nodes[i];
    return !strcmp(path,root)&&!nodes[1].present?&nodes[1]:!strcmp(path,tool)&&!nodes[2].present?&nodes[2]:!strcmp(path,daemon)&&!nodes[3].present?&nodes[3]:NULL;
}
static void existing_files(void) {
    node_init(1,VDIR,0711);node_init(2,VREG,0555);node_init(3,VREG,0555);
    memcpy(nodes[2].data,pong_bt_bluetool,pong_bt_bluetool_size);nodes[2].attrs.va_data_size=pong_bt_bluetool_size;
    memcpy(nodes[3].data,pong_bt_daemon,pong_bt_daemon_size);nodes[3].attrs.va_data_size=pong_bt_daemon_size;
}
int vnode_lookup(const char *path,int flags,vnode_t *vp,vfs_context_t ctx) {
    assert(ctx==&context&&(!strcmp(path,root)||!strcmp(path,tool)||!strcmp(path,daemon)));assert(flags&VNODE_LOOKUP_NOFOLLOW);
    if(scenario==24)return EIO;
    struct test_vnode *n=find(path);if(!n||!n->present)return ENOENT;
    n->refs++;*vp=n;return 0;
}
int vnode_getattr(vnode_t vp,struct vnode_attr *a,vfs_context_t ctx) {
    assert(ctx==&context&&vp&&vp->present&&vp->refs>0);
    if(scenario==36)return EIO;
    uint64_t active=a->va_active;*a=vp->attrs;a->va_active=active;a->va_supported&=active;return 0;
}
int vnode_isdir(vnode_t vp){return vp->type==VDIR;}
int vnode_isreg(vnode_t vp){return vp->type==VREG;}
int vnode_put(vnode_t vp){assert(vp->refs>0);vp->refs--;return 0;}
int namei(struct nameidata *nd) {
    assert(!strcmp((const char *)nd->ni_dirp,root)&&nd->ni_cnd.cn_context==&context);
    if(scenario==26)return EACCES;
    nodes[0].refs++;nd->ni_dvp=&nodes[0];
    if(scenario==22){node_init(1,VDIR,0711);nodes[1].refs++;nd->ni_vp=&nodes[1];}
    return 0;
}
void nameidone(struct nameidata *nd){assert(nd->ni_dvp);finalized++;}
int VNOP_MKDIR(vnode_t parent,vnode_t *vp,struct componentname *cn,struct vnode_attr *a,vfs_context_t ctx) {
    assert(parent==&nodes[0]&&cn->cn_context==ctx&&ctx==&context);
    assert(a->va_type==VDIR&&a->va_uid==0&&a->va_gid==0&&a->va_mode==0711);creates++;
    if(scenario==23)return ENOSPC;
    node_init(1,VDIR,scenario==28?0700:a->va_mode);
    if(scenario==27){*vp=NULL;return 0;}
    nodes[1].refs++;*vp=&nodes[1];return 0;
}
int vnode_open(const char *path,int mode,int cmode,int flags,vnode_t *vp,vfs_context_t ctx) {
    assert(ctx==&context&&flags==0&&(mode&O_NOFOLLOW));
    struct test_vnode *n=find(path);assert(n);
    if(scenario==25)return EPERM;
    if(n->present&&n->type==VLNK)return ELOOP;
    if(mode&O_CREAT) {
        assert((mode&O_EXCL)&&(mode&FWRITE)&&cmode==0555);if(n->present)return EEXIST;
        node_init(n==&nodes[2]?2:3,VREG,0555);
        if(scenario==29)n->attrs.va_supported&=~VNODE_ATTR_va_nlink;
    } else if(!n->present)return ENOENT;
    n->refs++;n->opens++;*vp=n;return 0;
}
int vnode_close(vnode_t vp,int flags,vfs_context_t ctx) {
    assert(ctx==&context&&vp->opens>0);vp->opens--;vnode_put(vp);
    return scenario==20&&(flags&FWRITE)?EIO:0;
}
int vn_rdwr(int direction,vnode_t vp,caddr_t data,int count,uint64_t offset,int segment,int flags,kauth_cred_t cred,int *resid,proc_t p) {
    assert(vp->refs>0&&segment==UIO_SYSSPACE&&(flags&IO_NOAUTH)&&cred==context.uid&&p==&process);
    assert(count>=0&&offset+(unsigned)count<=sizeof(vp->data));*resid=0;
    if(direction==UIO_WRITE) {
        writes++;assert((flags&IO_UNIT)&&offset==0);
        if(scenario==17||(scenario==30&&vp==&nodes[3]))return ENOSPC;
        int written=scenario==18?count/2:count;memcpy(vp->data,data,(unsigned)written);vp->attrs.va_data_size=(unsigned)written;*resid=count-written;
        if(scenario==21)vp->data[0]^=1;
    } else {assert(direction==UIO_READ&&offset+(unsigned)count<=vp->attrs.va_data_size);memcpy(data,vp->data+offset,(unsigned)count);}
    return 0;
}
int VNOP_FSYNC(vnode_t vp,int wait,vfs_context_t ctx){assert(vp->refs>0&&wait==MNT_WAIT&&ctx==&context);fsyncs++;return scenario==19?EIO:0;}
proc_t vfs_context_proc(vfs_context_t ctx){return ctx->process;}
kauth_cred_t vfs_context_ucred(vfs_context_t ctx){return ctx->uid;}
unsigned kauth_cred_getuid(kauth_cred_t cred){return cred;}
int proc_pid(proc_t p){return p->pid;}
vfs_context_t vfs_context_current(void){return &context;}
vnode_t proc_getexecutablevnode(proc_t p){vnode_t n=p->executable;if(n)n->refs++;return n;}
int vn_getpath(vnode_t vp,char *path,int *n){const char *value=vp->display_path[0]?vp->display_path:vp->path;int need=(int)strlen(value)+1;assert(*n>=need);memcpy(path,value,(unsigned)need);*n=need;return 0;}
static void reset(int value) {
    memset(nodes,0,sizeof(nodes));scenario=value;writes=fsyncs=creates=finalized=0;runtime_stage=0;runtime_error=0;memset(redirected,0,sizeof redirected);auth_traces=0;
    process=(struct test_proc){.pid=1,.uid=0};context=(struct test_context){.process=&process,.uid=0};node_init(0,VDIR,01777);
}
int main(void) {
    unsigned cases=0;
    for(int value=1;value<=38;value++) {
        reset(value);
        if(value>=2&&value<=16)existing_files();
        if(value==3)nodes[1].type=VLNK;
        if(value==4)nodes[1].attrs.va_uid=501;
        if(value==5)nodes[1].attrs.va_gid=501;
        if(value==6)nodes[1].attrs.va_mode=0777;
        if(value==7)nodes[1].attrs.va_supported&=~VNODE_ATTR_va_mode;
        if(value==8)nodes[2].attrs.va_uid=501;
        if(value==9)nodes[2].attrs.va_gid=501;
        if(value==10)nodes[2].attrs.va_mode=0755;
        if(value==11)nodes[2].attrs.va_mode=04555;
        if(value==12)nodes[2].attrs.va_nlink=2;
        if(value==13)nodes[2].attrs.va_data_size--;
        if(value==14)nodes[2].data[0]^=1;
        if(value==15)nodes[2].type=VLNK;
        if(value==16)nodes[2].type=VDIR;
        if(value==31)process.uid=context.uid=501;
        if(value==32)process.pid=40;
        const char *original=value==33?"/usr/sbin/other":"/sbin/launchd.ios";
        assert(pong_bt_runtime_exec(original,&context)==original);
        bool success=value==1||value==2||value==34||value==35||value==37||value==38;
        assert((pong_bt_runtime_ready()!=0)==success);
        if(success) {
            assert(!strcmp(pong_bt_runtime_exec("/usr/sbin/BlueTool",&context),tool));
            assert(!strcmp(pong_bt_runtime_exec("/usr/sbin/bluetoothd",&context),daemon));
            assert(nodes[1].attrs.va_mode==0711&&nodes[2].attrs.va_mode==0555&&nodes[3].attrs.va_mode==0555);
            process.pid=65;process.uid=context.uid=501;process.executable=&nodes[3];assert(pong_bt_runtime_client_allowed(&process));
            process.executable=&nodes[2];assert(pong_bt_runtime_client_allowed(&process));
            if(value==34) {
                nodes[2].data[0]^=1;assert(!pong_bt_runtime_client_allowed(&process));nodes[2].data[0]^=1;
                strcpy(nodes[2].path,"/usr/sbin/BlueTool");assert(!pong_bt_runtime_client_allowed(&process));strcpy(nodes[2].path,tool);
                nodes[2].attrs.va_uid=501;assert(!pong_bt_runtime_client_allowed(&process));
                assert(!pong_bt_runtime_client_allowed(NULL));
            }
            if(value==35){int before=writes;process.pid=1;process.uid=context.uid=0;assert(pong_bt_runtime_exec(original,&context)==original&&writes==before);}
            if(value==37){strcpy(nodes[2].display_path,"/var/tmp/pong-bt82/BlueTool");assert(pong_bt_runtime_client_allowed(&process));}
            if(value==38){struct test_vnode different=nodes[2];different.refs=different.opens=0;strcpy(different.path,"/private/var/tmp/another/BlueTool");process.executable=&different;assert(!pong_bt_runtime_client_allowed(&process)&&!different.refs);process.executable=&nodes[2];}
        } else {
            const char *path="/usr/sbin/BlueTool";assert(pong_bt_runtime_exec(path,&context)==path);
            path="/usr/sbin/bluetoothd";assert(pong_bt_runtime_exec(path,&context)==path);
            assert(!pong_bt_runtime_client_allowed(&process));
        }
        if(value==2)assert(writes==0&&creates==0&&fsyncs==0);
        if(value>=3&&value<=16)assert(writes==0);
        if(value>=31&&value<=33)assert(writes==0&&creates==0&&runtime_stage==((value==31)?2u:0u));
        for(unsigned i=0;i<4;i++)assert(!nodes[i].refs&&!nodes[i].opens);
        cases++;
    }
    puts("PASS82 runtime installer: 38 recording-VFS cases; canonical installed-vnode plus exact-byte identity, display-path alias accepted but copied image rejected, PID1/root-only install, metadata/partial/race/I/O gates, original-path fallback and balanced vnode lifetime. Host only.");
    assert(cases==38);return 0;
}
