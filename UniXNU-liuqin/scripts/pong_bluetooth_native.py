"""Generate the narrowly adapted, pinned APSL pipe; no device or image I/O."""
from pathlib import Path
import json,shutil,subprocess

PIN='986d6319d1d2819b27ed2c9786776f10bf63cc76'

def transport_source(shared):
    source=(shared/'src/qbt_transport.c').read_text()
    edits={
        '#define BT_SK_FIFO 8192u':'extern int pong_bluetooth_native_client_allowed(void);\nstatic void pong_bt_register_sysctls(void);\n#define BT_SK_FIFO 8192u',
        '#include <skywalk/os_skywalk.h>':'struct ifnet;\n#include <skywalk/os_skywalk.h>',
        'static LCK_GRP_DECLARE(bt_sk_grp,"unixnu.bt.uart.sk");\nstatic LCK_MTX_DECLARE(bt_sk_lock,&bt_sk_grp);':
            'static lck_grp_t *bt_sk_grp;\nstatic lck_mtx_t *bt_sk_lock;',
        '    uint32_t unit,spec[3];':'    uint32_t unit,spec[3];\n    pid_t owner;',
        '    lck_mtx_lock(&bt_sk_lock);\n    if(bt_sk.connected) {lck_mtx_unlock(&bt_sk_lock);return EBUSY;}':
            '    if(!pong_bluetooth_native_client_allowed())return EPERM;\n    lck_mtx_lock(&bt_sk_lock);\n    if(bt_sk.connected) {lck_mtx_unlock(&bt_sk_lock);return EBUSY;}\n    bt_sk.owner=proc_pid(current_proc());',
        '    if((uint32_t)opt==0x800c5414u) {':
            '    if(!pong_bluetooth_native_client_allowed() || proc_pid(current_proc())!=bt_sk.owner)return EPERM;\n    if((uint32_t)opt==0x800c5414u) {',
        '        if(bt_sk.ncd) {lck_mtx_unlock(&bt_sk_lock);return 0;}':
            '        if(bt_sk.ncd) {bool same=!memcmp(bt_sk.spec,spec,12);lck_mtx_unlock(&bt_sk_lock);return same?0:EBUSY;}',
        '    errno_t e=0;\n    lck_mtx_lock(&bt_sk_lock);\n    if((uint32_t)opt==0x400c5413u) {':
            '    errno_t e=0;\n    if(!pong_bluetooth_native_client_allowed() || proc_pid(current_proc())!=bt_sk.owner)return EPERM;\n    lck_mtx_lock(&bt_sk_lock);\n    if(!bt_sk.connected || bt_sk.closing){lck_mtx_unlock(&bt_sk_lock);return ENOTCONN;}\n    if((uint32_t)opt==0x400c5413u) {',
        '        lck_mtx_lock(&bt_sk_lock);bt_sk.termios=*t;lck_mtx_unlock(&bt_sk_lock);':
            '        lck_mtx_lock(&bt_sk_lock);if(!bt_sk.connected || bt_sk.closing){lck_mtx_unlock(&bt_sk_lock);return ENOTCONN;}bt_sk.termios=*t;lck_mtx_unlock(&bt_sk_lock);',
        'bool ok=(t->cflag&(0x300|0x400|0x1000))==0x300':
            'bool ok=(t->cflag&(0x300|0x400|0x800|0x1000))==(0x300|0x800)',
        '    int error=0,value;\n    bt_sk_count(1);':
            '    int error=0,value;\n    if(!bt_sk_lock)return ENODEV;\n    bt_sk_count(1);',
        '    if(!ops || !ops->wake || !ops->power)return EINVAL;':
            '    if(!ops || !ops->wake || !ops->power)return EINVAL;\n    if(!bt_sk_lock){\n        bt_sk_grp=lck_grp_alloc_init("unixnu.bt.uart.sk",LCK_GRP_ATTR_NULL);\n        if(!bt_sk_grp)return ENOMEM;\n        bt_sk_lock=lck_mtx_alloc_init(bt_sk_grp,LCK_ATTR_NULL);\n        if(!bt_sk_lock){lck_grp_free(bt_sk_grp);bt_sk_grp=NULL;return ENOMEM;}\n    }',
        '    bt_ops=*ops;lck_mtx_unlock(&bt_sk_lock);return 0;':
            '    bt_ops=*ops;lck_mtx_unlock(&bt_sk_lock);\n    pong_bt_register_sysctls();return 0;',
        '    int error=0,result=0;\n    if(req->newptr)return EPERM;':
            '    int error=0,result=0;\n    if(!bt_sk_lock)return ENODEV;\n    if(!pong_bluetooth_native_client_allowed())return EPERM;\n    if(req->newptr)return EPERM;',
    }
    for old,new in edits.items():
        if source.count(old)!=1:raise RuntimeError('Pinned native pipe anchor mismatch: '+old)
        source=source.replace(old,new,1)
    # Same current-PID value via the documented Darwin24 KPI, without an
    # undeclared current_proc() private helper in exported framework headers.
    source=source.replace('proc_pid(current_proc())','proc_selfpid()')
    # These headers are unused by the pipe and are not exported by Darwin24.
    # Keep real Skywalk declarations; do not supply fabricated compatibility APIs.
    for header in ('sys/mcache.h','mach/mach_interface.h','mach/semaphore.h','mach/sync_policy.h','kern/sync_sema.h','kern/task.h'):
        token='#include <'+header+'>\n'
        if source.count(token)!=1:raise RuntimeError('Pinned include anchor mismatch: '+header)
        source=source.replace(token,'',1)
    source=source.replace('WCN7850 kernel bring-up result','Qualcomm HCI-backed UART pipe result').replace('WCN7850 firmware build label','Reported resident controller identity').replace('WCN7850 startup request','Qualcomm controller startup request')
    source+='''
/* The matched22H374 deepsleep script needs a real power-off request too.
 * Reading submits the same owner-worker operation, then waits for its actual
 * result. No reply is manufactured for a missing/refused/timed-out worker. */
static int pong_bt_power_off_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp,arg1,arg2)
    int error=0,result=0;
    if(!bt_sk_lock)return ENODEV;
    if(!pong_bluetooth_native_client_allowed() || req->newptr)return EPERM;
    if(!req->oldptr)return SYSCTL_OUT(req,&result,sizeof(result));
    bt_sk_count(0);
    lck_mtx_lock(&bt_sk_lock);
    if(bt_sk.boot==ENETDOWN && !bt_sk.power_pending) {
        lck_mtx_unlock(&bt_sk_lock);return SYSCTL_OUT(req,&result,sizeof(result));
    }
    if(bt_sk.power_pending || !bt_ops.power){lck_mtx_unlock(&bt_sk_lock);return EBUSY;}
    bt_sk.power_pending=true;bt_sk.power_result=EINPROGRESS;
    lck_mtx_unlock(&bt_sk_lock);
    error=bt_ops.power(bt_ops.context,false);
    lck_mtx_lock(&bt_sk_lock);
    if(error){bt_sk.power_pending=false;bt_sk.power_result=error;}
    struct timespec timeout={30,0};
    while(bt_sk.power_pending && !error)error=msleep(&bt_sk.power_pending,&bt_sk_lock,PZERO|PCATCH,"btstop",&timeout);
    result=bt_sk.power_result;lck_mtx_unlock(&bt_sk_lock);
    return error?error:SYSCTL_OUT(req,&result,sizeof(result));
}
SYSCTL_PROC(_kern,OID_AUTO,qbt_power_off,CTLTYPE_INT|CTLFLAG_RD|CTLFLAG_LOCKED,NULL,0,pong_bt_power_off_sysctl,"I","Owner-worker controller stop result");

static void pong_bt_register_sysctls(void)
{
    /* Darwin24 exported SYSCTL_OID declares storage only. This board archive
     * is not the core kernel's startup linker set: register the real OIDs
     * explicitly, after a valid worker binding and outside the pipe lock.
     * qbt_transport_init's existing bound-ops guard makes this once-only. */
    sysctl_register_oid(&sysctl__kern_qbt_boot);
    sysctl_register_oid(&sysctl__kern_qbt_firmware);
    sysctl_register_oid(&sysctl__kern_qbt_power_on);
    sysctl_register_oid(&sysctl__kern_qbt_power_off);
    printf("Pong Bluetooth82: explicit registration invoked for4 native bootstrap sysctls; controller readiness still requires actual worker result\\n");
}

/* Consumer diagnostics: real registration state and actual native ring I/O,
 * not simulated HCI replies or pairing/HID completion. */
int pong_bluetooth_native_status(void)
{
    if(!bt_sk_lock)return ENODEV;
    lck_mtx_lock(&bt_sk_lock);
    int result=bt_sk.boot?bt_sk.boot:bt_sk.ctl?0:ENODEV;
    lck_mtx_unlock(&bt_sk_lock);return result;
}
void pong_bluetooth_native_counters(uint64_t values[4])
{
    if(!bt_sk_lock){memset(values,0,4*sizeof(*values));values[3]=ENODEV;return;}
    lck_mtx_lock(&bt_sk_lock);
    values[0]=bt_sk_kicks_tx;values[1]=bt_sk_kicks_rx;
    values[2]=bt_sk.rx_dropped;values[3]=(uint32_t)bt_sk.boot;
    lck_mtx_unlock(&bt_sk_lock);
}
'''
    source=source.replace('&bt_sk_lock','bt_sk_lock')
    return source

def prepare_transport(shared,out):
    shared=Path(shared);out=Path(out);out.mkdir(parents=True,exist_ok=True)
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=shared,text=True).strip()
    if revision!=PIN:raise RuntimeError('Native UART.SK revision differs; source retained')
    dirty=subprocess.check_output(['git','status','--porcelain','--','src/qbt_transport.c','include/qbt_transport.h'],cwd=shared,text=True)
    if dirty:raise RuntimeError('Modified native pipe input retained; do not overwrite it')
    target=out/'PongBluetoothNativeTransport.c';source=transport_source(shared)
    if target.exists() and target.read_text()!=source:raise RuntimeError('Differing generated native pipe retained')
    target.write_text(source)
    shutil.copyfile(shared/'src/qbt_transport.c.license',out/'PongBluetoothNativeTransport.c.license')
    record=dict(component='QcaBluetooth',revision=revision,source=str(shared/'src/qbt_transport.c'),generated=str(target),license='APSL-2.0',
                scope='hardware-independent UART.SK only; liuqin FIFO/clock/power owner, no SM8550 DMA/firmware or iPadOS27 native patches',
                adaptation='explicit Darwin24 sysctl registration, signed/entitled native client and PID ownership, unchanged live channel specification, real worker power/state/ring results')
    (out/'bluetooth-native-source.json').write_text(json.dumps(record,indent=2)+'\n')
    return target,shared/'include'
