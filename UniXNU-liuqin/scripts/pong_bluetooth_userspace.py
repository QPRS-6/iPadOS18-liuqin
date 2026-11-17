"""Local owner-built81 runtime and address inputs. Never publish proprietary bytes/MAC."""
from pathlib import Path
import json
import shutil

def prepare_runtime(runtime,address,out):
    runtime=Path(runtime);address=Path(address);out=Path(out);out.mkdir(parents=True,exist_ok=True)
    report=json.loads((runtime/'report.json').read_text())
    if report.get('build')!='22H374' or report.get('uuid')!='D3FB9884-D7FB-3B11-A807-5CFB1845C2C8' or not report.get('compiled'):raise RuntimeError('Matching compiled22H374 runtime required')
    addr=json.loads(address.read_text());raw=bytes.fromhex(addr['network_order_hex'])
    if len(raw)!=6 or raw in (bytes(6),bytes([255])*6,bytes.fromhex('000000005aad')) or raw[0]&1:raise RuntimeError('Invalid owner address input')
    if not addr.get('requires_controller_readback_match') or addr.get('log_value') or addr.get('publish_to_git'):raise RuntimeError('Private address/readback policy required')
    (out/'PongBluetoothRuntimeData.h').write_text('extern const unsigned char pong_bt_bluetool[],pong_bt_daemon[];\nextern const unsigned pong_bt_bluetool_size,pong_bt_daemon_size;\n')
    (out/'PongBluetoothAddress.h').write_text('/* Private owner input; never log or publish this generated file. */\nstatic const unsigned char pong_bt_owner_address[6]={'+','.join(str(b) for b in raw)+'};\n')
    (out/'PongBluetoothAddress.h').chmod(0o600)
    asm=[]
    for name,symbol in (('BlueTool','bluetool'),('bluetoothd','daemon')):
        file=(runtime/name).resolve()
        if not file.is_file():raise RuntimeError('Compiled runtime file missing')
        if '"' in str(file) or '\n' in str(file):raise RuntimeError('Unsafe generated assembler path')
        asm += ['.section __TEXT,__const','.p2align 3',f'.globl _pong_bt_{symbol}',f'_pong_bt_{symbol}:',f'.incbin "{file}"',f'_pong_bt_{symbol}_end:','.p2align 2',f'.globl _pong_bt_{symbol}_size',f'_pong_bt_{symbol}_size:',f'.long _pong_bt_{symbol}_end-_pong_bt_{symbol}']
    source=out/'PongBluetoothRuntimeData.S';source.write_text('\n'.join(asm)+'\n')
    (out/'bluetooth-userspace-source.json').write_text(json.dumps(dict(build='22H374',runtime=str(runtime),address_input=str(address),address_value_logged=False,scope='owner runtime inData; originalOS/Data seed/GPU unchanged'),indent=2)+'\n')
    return source

def private_vfs_headers(tree,out):
    # Use exactly the selected Darwin24 declarations omitted by exported SDK
    # headers. Do not override unrelated BSD/system headers or invent layouts.
    tree=Path(tree);target=Path(out)/'bt-vfs-private/sys';target.mkdir(parents=True,exist_ok=True)
    for name in ('namei.h','vnode.h','vnode_if.h','filedesc.h'):
        source=tree/'refer/xnu/bsd/sys'/name
        if not source.is_file():raise RuntimeError('Selected XNU private VFS declaration missing')
        shutil.copyfile(source,target/name)
    return target.parent
