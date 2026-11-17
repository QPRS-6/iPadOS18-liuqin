#!/usr/bin/env python3
"""Capture current unmounted liuqin hfs, update one UI file, package/flash safely.

This capture is a current filesystem update source, NOT image verification.
No hashes, benchmarks or host/device image comparisons. OS/SystemOS/Data and
all bytes outside the edited OS image come from the newly captured device.
"""
import argparse
from datetime import datetime, timezone
import gzip
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shlex
import shutil
import stat
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MIB = 1048576
TOTAL = 32 << 30


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def directory(data):
    if len(data) != 4096 or data[:8] != b'SHNGROOT':
        raise RuntimeError('current hfs is not the expected port filesystem')
    version, count, total = struct.unpack_from('<IIQ', data, 8)
    if version != 1 or count != 3 or total != TOTAL:
        raise RuntimeError('unsupported current hfs directory geometry')
    windows = []
    for i in range(count):
        name, offset, size = struct.unpack_from('<32sQQ', data, 24 + i * 128)
        name = name.split(b'\0')[0].decode('ascii')
        if offset < MIB or offset % 4096 or not size or size % 4096 or offset + size > total:
            raise RuntimeError('window outside current32GiB hfs')
        windows.append(dict(name=name, offset=offset, size=size))
    windows.sort(key=lambda w: w['offset'])
    if {w['name'] for w in windows} != {'OS', 'SystemOS', 'Data'}:
        raise RuntimeError('unexpected current hfs windows')
    for before, after in zip(windows, windows[1:]):
        if before['offset'] + before['size'] > after['offset']:
            raise RuntimeError('overlapping current hfs windows')
    return windows


class UpdatedSource:
    def __init__(self, captured, edited_os, offset, size):
        self.captured, self.edited_os, self.offset, self.size = captured, edited_os, offset, size

    def open(self, mode, buffering=0):
        return UpdatedStream(self)


class UpdatedStream:
    def __init__(self, source):
        self.source = source
        self.old = gzip.open(source.captured, 'rb')
        self.updated = source.edited_os.open('rb')
        self.at = 0

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.old.close()
        self.updated.close()

    def read(self, amount):
        data = self.old.read(amount)
        if len(data) != amount:
            raise RuntimeError('incomplete current hfs capture; no flash allowed')
        start = max(self.at, self.source.offset)
        end = min(self.at + amount, self.source.offset + self.source.size)
        if start < end:
            self.updated.seek(start - self.source.offset)
            replacement = self.updated.read(end - start)
            if len(replacement) != end - start:
                raise RuntimeError('edited OS source incomplete')
            data = data[:start - self.at] + replacement + data[end - self.at:]
        self.at += amount
        return data


def capture_device(a, out):
    serial = a.serial
    if not serial:
        devices = subprocess.check_output([str(a.adb), 'devices'], text=True).splitlines()[1:]
        candidates = [line.split()[0] for line in devices if len(line.split()) >= 2 and line.split()[1] in ('device', 'recovery')]
        if len(candidates) != 1:
            raise RuntimeError('connect exactly one liuqin in recovery, or specify --serial')
        serial = candidates[0]
    adb = [str(a.adb), '-s', serial]

    def shell(text):
        return subprocess.check_output(adb + ['shell', text], text=True).strip()

    state = subprocess.check_output(adb + ['get-state'], text=True).strip()
    if state != 'recovery' and shell('getprop ro.bootmode') != 'recovery':
        raise RuntimeError('current filesystem capture requires recovery, not a running guest')
    if shell('getprop ro.product.device').lower() != 'liuqin':
        raise RuntimeError('wrong device: only liuqin hfs is authorized')
    target = '/dev/block/by-name/hfs'
    real = shell('readlink -f ' + target)
    if not real.startswith('/dev/block/') or int(shell('blockdev --getsize64 ' + target)) != TOTAL:
        raise RuntimeError('hfs target missing or not32GiB; no fallback partition or writes')

    def unmounted():
        if shell('ls /sys/class/block/' + shlex.quote(Path(real).name) + '/holders'):
            raise RuntimeError('hfs has active block holders; leave it quiescent in recovery')
        for line in shell('cat /proc/mounts').splitlines():
            fields = line.split()
            if fields and fields[0] in (real, target):
                raise RuntimeError('hfs is mounted; no automatic unmount or capture')

    unmounted()
    header = subprocess.check_output(adb + ['exec-out', f'dd if={target} bs=4096 count=1 status=none'])
    windows = directory(header)
    os_window = next(w for w in windows if w['name'] == 'OS')
    if os_window['size'] > 8 << 30:
        raise RuntimeError('unsupported OS source size')
    out.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(out.parent).free < os_window['size'] + (3 << 30):
        raise RuntimeError('insufficient space for live source capture; no device write')
    out.mkdir(parents=True)
    captured = out / 'current-hfs.raw.gz'
    process = subprocess.Popen(adb + ['exec-out', f'set -o pipefail; dd if={target} bs=1048576 count={TOTAL//MIB} status=none | gzip -1 -c'], stdout=subprocess.PIPE)
    try:
        with captured.open('xb') as stream:
            while True:
                data = process.stdout.read(MIB)
                if not data:
                    break
                if shutil.disk_usage(out).free < 2 << 30:
                    raise RuntimeError('capture stopped before filling host volume; no device write')
                stream.write(data)
        if process.wait():
            raise RuntimeError('current recovery filesystem capture failed')
        unmounted()
    except BaseException:
        if process.poll() is None:
            process.kill()
            process.wait()
        raise
    return captured, serial, real, windows, unmounted, adb


def supplied_capture(path, record):
    """Consume the current REC source collected on the user's Linux host."""
    path, record = path.resolve(), record.resolve()
    info = json.loads(record.read_text())
    if info.get('state') != 'complete' or info.get('device_product') != 'liuqin' or \
       info.get('target') != '/dev/block/by-name/hfs' or info.get('logical_bytes') != TOTAL or \
       info.get('physical_bytes') != TOTAL or info.get('current_unmounted_filesystem') is not True or \
       info.get('device_writes') is not False:
        raise RuntimeError('a completed current liuqin REC capture receipt is required')
    if not path.is_file() or Path(info.get('capture', '')).name != path.name:
        raise RuntimeError('capture file/receipt association differs')
    with gzip.open(path, 'rb') as stream:
        windows = directory(stream.read(4096))
    return path, info.get('serial'), info.get('device'), windows


def attach_copy(image):
    """Mount only our owned image copy; data-only edits need no chown/sudo.

    -owners off on this temporary disk image permits updating an existing file
    while its stored inode owner/mode remain unchanged. No host filesystem or
    global ownership/security settings are modified.
    """
    mounted = Path(tempfile.mkdtemp(prefix='native50-os-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode'))
    owners = 'on' if os.geteuid() == 0 else 'off'
    result = subprocess.run(['hdiutil', 'attach', '-readwrite', '-nobrowse', '-noverify', '-owners', owners,
                             '-mountpoint', str(mounted), '-plist', str(image)], capture_output=True, check=True)
    entities = plistlib.loads(result.stdout)['system-entities']
    whole = next(item['dev-entry'] for item in entities if item.get('dev-entry'))
    if not any(item.get('mount-point') == str(mounted) for item in entities):
        raise RuntimeError('owned OS image mount was not returned')
    return whole, mounted, owners


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb', type=Path)
    p.add_argument('--capture', type=Path, help='Current-hfs gzip source captured on Linux; no USB passthrough required')
    p.add_argument('--capture-receipt', type=Path)
    p.add_argument('--fastboot', type=Path)
    p.add_argument('--serial', help='Omit only if exactly one liuqin recovery device is connected')
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--boot-image', type=Path)
    p.add_argument('--deployment-record', type=Path, help='Update candidate metadata only after real packaging/install commands')
    p.add_argument('--flash', action='store_true', help='Explicitly write hfs with -S512M, then temporary boot')
    a = p.parse_args()
    if bool(a.adb) == bool(a.capture):
        p.error('choose exactly one --adb live capture or --capture Linux source')
    if a.capture and a.flash:
        p.error('Linux capture mode only generates simg; the owner flashes on Linux')
    if a.flash and (not a.fastboot or not a.boot_image or not a.boot_image.is_file()):
        p.error('--flash requires --fastboot and an existing --boot-image')
    runtime, out, output = a.runtime.resolve(), a.out.resolve(), a.output.resolve()
    if out.exists() or output.exists():
        raise RuntimeError('refusing to overwrite existing capture/root candidate')
    info = json.loads((runtime / 'helper.json').read_text())
    if info.get('build') != '22H374' or info.get('ui_library_runtime') != '/pong/libpong-ui18.dylib' or info.get('native_pen_abi_bytes') != 48:
        raise RuntimeError('matching native50 runtime required')
    target = '/dev/block/by-name/hfs'
    if a.capture:
        record = a.capture_receipt or a.capture.with_name(a.capture.name + '.json')
        captured, serial, real, windows = supplied_capture(a.capture, record)
        out.mkdir(parents=True)
    else:
        captured, serial, real, windows, unmounted, adb = capture_device(a, out)
    os_window = next(w for w in windows if w['name'] == 'OS')
    if os_window['size'] > 8 << 30:
        raise RuntimeError('unsupported OS source size')
    if shutil.disk_usage(out).free < os_window['size'] + (3 << 30):
        raise RuntimeError('insufficient space for current OS copy; no filesystem edit')
    # Capture is an update source, never an image-comparison/verification pass.
    os_image = out / 'current-system.cdr'
    done = 0
    with gzip.open(captured, 'rb') as source, os_image.open('xb') as destination:
        while done < TOTAL:
            data = source.read(min(4 * MIB, TOTAL - done))
            if not data:
                raise RuntimeError('short current hfs source; no edit/flash')
            start = max(done, os_window['offset'])
            end = min(done + len(data), os_window['offset'] + os_window['size'])
            if start < end:
                destination.write(data[start-done:end-done])
            done += len(data)
        if source.read(1):
            raise RuntimeError('current hfs source exceeds bounded geometry')
    if os_image.stat().st_size != os_window['size']:
        raise RuntimeError('current OS capture incomplete')
    (out / 'begin.json').write_text(json.dumps(dict(
        phase='begin', current_unmounted_capture=True, serial=serial, target=target, device=real,
        windows=windows, total_bytes=TOTAL, capture=str(captured),
        image_verification=False, hashes=False, created_utc=datetime.now(timezone.utc).isoformat()), indent=2) + '\n')
    device, mounted, owners = attach_copy(os_image)
    try:
        version = plistlib.loads((mounted / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
        if version.get('ProductBuildVersion') != '22H374':
            raise RuntimeError('captured guest is not22H374; no runtime replacement')
        destination = mounted / 'pong/libpong-ui18.dylib'
        if destination.is_symlink() or not destination.is_file():
            raise RuntimeError('captured OS lacks the expected regular UI adapter')
        if not destination.resolve().is_relative_to(mounted.resolve()):
            raise RuntimeError('UI adapter parent escapes the captured OS mount; no replacement')
        before = destination.stat()
        metadata = (before.st_uid, before.st_gid, stat.S_IMODE(before.st_mode), before.st_flags)
        shutil.copyfile(destination, out / 'libpong-ui18.previous.dylib')
        shutil.copyfile(runtime / 'libpong-ui.dylib', destination)
        after = destination.stat()
        if metadata != (after.st_uid, after.st_gid, stat.S_IMODE(after.st_mode), after.st_flags):
            raise RuntimeError('data-only UI edit changed inode permissions; no root packaging')
    finally:
        subprocess.run(['hdiutil', 'detach', device], check=True)
        mounted.rmdir()  # never recursively remove a possibly mounted filesystem
    (out / 'stage.json').write_text(json.dumps(dict(phase='stage', changed_files=['/pong/libpong-ui18.dylib'],
        current_device_base=True, inode_permissions_preserved=True, copy_mount_owners=owners,
        data_modified=False, systemos_modified=False, metal_modified=False), indent=2) + '\n')
    if shutil.disk_usage(out).free < TOTAL + MIB:
        raise RuntimeError('need32GiB worst-case output space; current capture/edit retained, no flash')
    sparse = load(ROOT / 'scripts/build-pong-userdata-simg.py', 'live_native50_sparse')
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise RuntimeError('another file appeared at the root candidate path; refusing overwrite')
    writer = sparse.SparseWriter(output, TOTAL)
    try:
        sparse.copy_window(writer, UpdatedSource(captured, os_image, os_window['offset'], os_window['size']), TOTAL//4096, 'current hfs + one UI file')
        writer.finish()
    except BaseException:
        writer.handle.close()
        raise
    receipt = dict(phase='finish', current_device_filesystem_preserved=True, target=target, serial=serial,
        sparse_image=str(output), logical_bytes=TOTAL, sparse_bytes=output.stat().st_size,
        changed_files=['/pong/libpong-ui18.dylib'], data_modified=False, systemos_modified=False,
        metal_modified=False, image_verification='skipped by request', hashes=False, flashed=False)
    (out / 'finish.json').write_text(json.dumps(receipt, indent=2) + '\n')
    def update_deployment(fields):
        if a.deployment_record:
            path = a.deployment_record.resolve()
            current = json.loads(path.read_text())
            if current.get('native_selector') != 12 or current.get('matching_runtime') != str(runtime / 'libpong-ui.dylib'):
                raise RuntimeError('deployment record does not match this native50 runtime')
            current.update(fields)
            path.write_text(json.dumps(current, indent=2) + '\n')
    update_deployment(dict(root_ready=True, root=str(output), root_capture=str(out / 'begin.json'), root_finish=str(out / 'finish.json')))
    print('READY current-data-preserving root:', output, flush=True)
    if a.flash:
        unmounted()
        subprocess.run(adb + ['reboot', 'bootloader'], check=True)
        fastboot = [str(a.fastboot), '-s', serial]
        geometry = subprocess.run(fastboot + ['getvar', 'partition-size:hfs'], capture_output=True, text=True, timeout=90, check=True)
        import re
        match = re.search(r'partition-size:hfs:\s*(0x[0-9a-fA-F]+|[0-9]+)', geometry.stdout + geometry.stderr)
        if not match or int(match[1], 0) != TOTAL:
            raise RuntimeError('fastboot hfs geometry changed; no flash')
        subprocess.run(fastboot + ['-S', '512M', 'flash', 'hfs', str(output)], check=True)
        receipt['flashed'] = True
        (out / 'install.json').write_text(json.dumps(receipt, indent=2) + '\n')
        update_deployment(dict(runtime_installed=True, hfs_flash_command_succeeded=True, install_receipt=str(out / 'install.json')))
        subprocess.run(fastboot + ['boot', str(a.boot_image.resolve())], check=True)
        update_deployment(dict(host_boot_command_sent=True, device_boot_attempted=True, device_verified=False))
        print('hfs installed and native50 temporary boot sent; leave device running, functionality still unverified')


if __name__ == '__main__':
    main()
