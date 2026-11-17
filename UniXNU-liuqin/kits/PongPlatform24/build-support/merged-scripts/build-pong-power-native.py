#!/usr/bin/env python3
"""Build SM8475 ADSP/GLINK, Nothing battery protocol and audio GPR for Darwin24.
No private firmware is embedded in the archive. No phone I/O or image audits.
"""
import argparse,os,json,struct,subprocess
import sys
from pathlib import Path
from pong_native_inputs import build_env, input_path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import pong_paths
def late_init(obj, filename, symbol):
 raw=bytearray(obj.read_bytes());at=32;sym=None
 for _ in range(struct.unpack_from('<I',raw,16)[0]):
  kind,length=struct.unpack_from('<II',raw,at)
  if kind==0x19:
   for i in range(struct.unpack_from('<I',raw,at+64)[0]):
    sc=at+72+80*i
    if raw[sc:sc+16].rstrip(b'\0')==b'__mod_init_func':
     raw[sc:sc+16]=b'__pong_init'.ljust(16,b'\0');v=struct.unpack_from('<I',raw,sc+64)[0];struct.pack_into('<I',raw,sc+64,v&~255)
  elif kind==2:sym=at
  at+=length
 if sym is None:raise RuntimeError('Battery object has no symbol table')
 so,count,st,size=struct.unpack_from('<4I',raw,sym+8)
 if st+size!=len(raw):raise RuntimeError('Unexpected owned battery object layout')
 for i in range(count):
  at=so+16*i;p=st+struct.unpack_from('<I',raw,at)[0];z=raw.index(0,p)
  if raw[p:z]==('__GLOBAL__sub_I_'+filename).encode():
   new=('_'+symbol+'\0').encode();struct.pack_into('<I',raw,at,size);raw[at+4]|=1;raw.extend(new);struct.pack_into('<I',raw,sym+20,size+len(new));break
 else:raise RuntimeError('Battery initializer missing')
 dest=obj.with_suffix('.late.o');dest.write_bytes(raw);return dest

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tree',type=Path,required=True);a=p.parse_args()
 out=a.tree/'build/pong-power';out.mkdir(parents=True,exist_ok=True)
 minimal=os.environ.get('PONG_POWER_BACKLIGHT_ONLY')=='1'
 battery_only=os.environ.get('PONG_POWER_BATTERY_ONLY')=='1'
 if minimal and battery_only:raise RuntimeError('Select only one minimal power build')
 framework=a.tree/'.cache/fakeroot/System/Library/Frameworks/Kernel.framework/Versions/A'
 env=build_env()
 sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],env=env,text=True).strip()
 shared=input_path('sheng_source')/'package'
 flags=['-target','arm64-apple-ios18.0','-isysroot',sdk,'-mkernel','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-fblocks','-O2','-DKERNEL=1','-DKERNEL_PRIVATE=1','-DXNU_KERNEL_PRIVATE=1','-DARM64_BOARD_CONFIG_PONG=1','-I'+str(framework/'PrivateHeaders'),'-I'+str(framework/'Headers'),'-I'+str(ROOT/'drivers/PongPower'),'-I'+str(out),'-I'+str(shared),'-Wno-deprecated-declarations']
 if minimal: flags.append('-DPONG_POWER_BACKLIGHT_ONLY=1')
 if battery_only:flags += ['-DPONG_POWER_BATTERY_ONLY=1','-DPONG_BATTERY_LIUQIN=1']
 bluetooth=os.environ.get('PONG_BLUETOOTH')=='1'
 if bluetooth:flags.append('-DPONG_BLUETOOTH=1')
 native_bluetooth=os.environ.get('PONG_BLUETOOTH_NATIVE')=='1'
 if native_bluetooth and not bluetooth:raise RuntimeError('Native Bluetooth requires the real UART owner')
 if native_bluetooth:
  from pong_bluetooth_native import prepare_transport
  native_source,native_include=prepare_transport(input_path('bluetooth_shared'),out)
  flags += ['-DPONG_BLUETOOTH_NATIVE=1','-I'+str(native_include)]
 userspace_bluetooth=os.environ.get('PONG_BLUETOOTH_USERSPACE')=='1'
 if userspace_bluetooth and not native_bluetooth:raise RuntimeError('Bluetooth userspace requires the real native pipe')
 if userspace_bluetooth:
  from pong_bluetooth_userspace import prepare_runtime,private_vfs_headers
  runtime_source=prepare_runtime(os.environ['PONG_BLUETOOTH_RUNTIME'],os.environ['PONG_BLUETOOTH_ADDRESS'],out)
  private_headers=private_vfs_headers(a.tree,out)
  flags += ['-DPONG_BLUETOOTH_USERSPACE=1']
 if not minimal and not battery_only:
  subprocess.run([os.sys.executable,str(ROOT/'scripts/prepare-pong-audio.py'),'--container',str(input_path('audio_container')),'--output',str(out/'PongAmpProfiles.h')],check=True)
 objects=[]
 backlight='PongBacklightKtz.cpp' if os.environ.get('PONG_BACKLIGHT_KTZ8866')=='1' else 'PongBacklight.cpp'
 sources=('GeniI2c.c',backlight) if minimal else ('QcomRpmh.c','QcomSmem.c','QcomSmp2p.c','QcomGlink.c','QcomMdt.c','QcomQmp.c','ShengBattmgr.c','ShengGpr.c','ShengAudioReach.c','GeniI2c.c','PongFirmware.c','PongAdsp.cpp','PongBattery.cpp','PongAudio.cpp','PongAudioService.cpp','PongPlatformResources.cpp','PongPower.cpp',backlight)
 if battery_only:sources=('QcomRpmh.c','QcomSmem.c','QcomSmp2p.c','QcomGlink.c','QcomMdt.c','QcomQmp.c','ShengBattmgr.c','GeniI2c.c','PongFirmware.c','PongAdsp.cpp','PongBattery.cpp','PongPlatformResources.cpp','PongPower.cpp',backlight)
 if bluetooth:sources += ('PongBluetooth.cpp',)
 if native_bluetooth:sources += ('PongBluetoothNative.cpp','PongBluetoothNativeTransport.c')
 if userspace_bluetooth:sources += ('PongBluetoothRuntime.c','PongBluetoothRuntimeData.S')
 for name in sources:
  directory=out if name in ('PongBluetoothNativeTransport.c','PongBluetoothRuntimeData.S') else shared if name=='QcomRpmh.c' else ROOT/'drivers'/('PongBluetooth' if name.startswith('PongBluetooth') else 'PongPower')
  source=directory/name;obj=out/(source.stem+'.o');cpp=source.suffix=='.cpp'
  private=['-DBSD_KERNEL_PRIVATE=1','-I'+str(private_headers)] if name=='PongBluetoothRuntime.c' else []
  cmd=['xcrun','clang++' if cpp else 'clang',*private,*flags,*(['-std=gnu++17','-fno-exceptions','-fno-rtti'] if cpp else []),'-c',str(source),'-o',str(obj)]
  r=subprocess.run(cmd,env=env,capture_output=True,text=True);(out/(source.stem+'.log')).write_text(r.stdout+r.stderr)
  if r.returncode:raise SystemExit(r.stderr)
  if name=='PongBattery.cpp':obj=late_init(obj,name,'PongBatteryStaticInit')
  if name=='PongAudioService.cpp':obj=late_init(obj,name,'PongAudioServiceStaticInit')
  if name in ('PongBacklight.cpp','PongBacklightKtz.cpp'):obj=late_init(obj,name,'PongBacklightStaticInit')
  objects.append(obj)
 archive=out/'libPongPower.a';archive.unlink(missing_ok=True)
 subprocess.run(['xcrun','libtool','-static','-o',str(archive),*map(str,objects)],env=env,check=True)
 (out/'build-report.json').write_text(json.dumps(dict(board='SM8475 Pong',guest='22H374',adsp_pas=1,adsp_carveout=[0x85700000,0x2800000],ipcc=0xed18000,smem=0x80900000,battery='Nothing standard PMIC GLINK, single pack, read only',audio='adsp_apps APM graphs, SID1801, TFA9873 stereo PCM ring; no microphone endpoint',firmware='Local REC image staged to /pong/firmware/adsp.mbn',runtime_observed=False),indent=2)+'\n')
 if battery_only:
  (out/'build-report.json').write_text(json.dumps(dict(board='liuqin / SM8475',guest='22H374',adsp_pas=1,adsp_carveout=[0x9fd00000,0x3100000],ipcc=0xed18000,smem=0x80900000,battery='Xiaomi M81 standard PMIC GLINK, read only',audio='not built or started',firmware='liuqin OS3.0.7.0.VMYCNXM NON-HLOS',runtime_observed=False),indent=2)+'\n')
 print(archive)
if __name__=='__main__':main()
