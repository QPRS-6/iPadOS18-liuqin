#!/usr/bin/env python3
"""Compile the independent SM8475 GENI/GPI transport; no device execution."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--firmware', type=Path, default=os.environ.get('PONG_NATIVE_TOUCH_FIRMWARE'),
                        help='Local liuqin panel firmware data; never old driver/loader code')
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    framework = args.tree.resolve() / '.cache/fakeroot/System/Library/Frameworks/Kernel.framework/Versions/A'
    sdk = os.environ.get('PONG_IOS_SDK') or subprocess.check_output(
        ['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
    source = ROOT / 'drivers/PongInputNative/PongInputNative.cpp'
    obj = out / 'PongInputNative.o'
    command = ['xcrun', 'clang++', '-target', 'arm64-apple-ios18.0', '-isysroot', sdk,
        '-mkernel', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
        '-mgeneral-regs-only', '-fno-exceptions', '-fno-rtti', '-fno-stack-check',
        '-std=gnu++17', '-fblocks', '-Os', '-DKERNEL=1', '-DKERNEL_PRIVATE=1',
        '-DXNU_KERNEL_PRIVATE=1', '-DARM64_BOARD_CONFIG_PONG=1',
        '-I' + str(framework / 'PrivateHeaders'), '-I' + str(framework / 'Headers'),
        '-Wno-deprecated-declarations', '-Wall', '-Wextra', '-Werror',
        '-c', str(source), '-o', str(obj)]
    if args.firmware:
        firmware = args.firmware.resolve()
        data = firmware.read_bytes()
        if len(data) != 245760:
            raise SystemExit('Expected liuqin panel firmware input of245760 bytes')
        include = out / 'PongInputNativeFirmware.inc'
        include.write_text('static const uint8_t nativeFirmware[] = {\n' +
            '\n'.join(','.join(f'0x{v:02x}' for v in data[at:at+16]) + ',' for at in range(0,len(data),16)) +
            '\n};\nstatic constexpr unsigned nativeFirmwareBytes=sizeof(nativeFirmware);\n')
        command[2:2] = ['-DPONG_NATIVE_FIRMWARE_INCLUDED=1', '-I' + str(out)]
    subprocess.run(command, check=True)
    pen_source = ROOT / 'drivers/PongInputNative/PongNativePenQueue.c'
    pen_obj = out / 'PongNativePenQueue.o'
    pen_command = [value.replace('clang++', 'clang').replace('gnu++17', 'gnu11')
                   for value in command if value not in ('-fno-rtti', '-fno-exceptions')]
    pen_command[pen_command.index(str(source))] = str(pen_source)
    pen_command[pen_command.index(str(obj))] = str(pen_obj)
    subprocess.run(pen_command, check=True)
    archive = out / 'libPongInputNative.a'
    subprocess.run(['xcrun', 'libtool', '-static', '-o', str(archive), str(obj), str(pen_obj)], check=True)
    (out / 'build-report.json').write_text(json.dumps({
        'guest': '22H374', 'target': 'liuqin / SM8475', 'command': command,
        'archive': str(archive), 'source': str(source), 'legacy_touch_dependency': False,
        'gpio_spi_fallback': False, 'hardware_verified': False,
        'firmware_input': str(args.firmware.resolve()) if args.firmware else None,
        'pen_queue_command': pen_command,
        'stage': 'mode-dependent native transport/firmware/report or touch/pen queue delivery; device validation required'
    }, indent=2) + '\n')
    print(archive)

if __name__ == '__main__':
    main()
