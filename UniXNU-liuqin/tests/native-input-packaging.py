#!/usr/bin/env python3
"""Host-only bounds/remapping/HID-descriptor tests; never reads a device image."""
import gzip
import importlib.util
import json
from pathlib import Path
import re
import struct
import tempfile

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('live_root_test', repo / 'scripts/build-pong-native-input-live-root.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
header = bytearray(4096)
header[:8] = b'SHNGROOT'
struct.pack_into('<IIQ', header, 8, 1, 3, module.TOTAL)
for i, name in enumerate(('OS', 'SystemOS', 'Data')):
    struct.pack_into('<32sQQ', header, 24 + i * 128, name.encode(), module.MIB + i * 4096, 4096)
assert len(module.directory(header)) == 3
bad = bytearray(header)
struct.pack_into('<Q', bad, 24 + 128 + 32, module.MIB)
try:
    module.directory(bad)
    raise AssertionError('overlap accepted')
except RuntimeError:
    pass
with tempfile.TemporaryDirectory(prefix='native-package-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    original = bytes(range(256)) * 32
    captured = temp / 'source.gz'
    with gzip.open(captured, 'wb') as stream:
        stream.write(original)
    updated = temp / 'edited-os'
    edited = bytes([19]) * 2100
    updated.write_bytes(edited)
    source = module.UpdatedSource(captured, updated, 1000, len(edited))
    with source.open('rb') as stream:
        actual = b''.join(stream.read(n) for n in (512, 1500, 1024, 4096, 1060))
    assert len(actual) == len(original)
    assert actual[:1000] == original[:1000] and actual[3100:] == original[3100:]
    assert actual[1000:3100] == edited
    supplied = temp / 'current-hfs.gz'
    with gzip.open(supplied, 'wb') as stream:
        stream.write(header)
    receipt = temp / 'current-hfs.gz.json'
    metadata = dict(state='complete', device_product='liuqin', target='/dev/block/by-name/hfs',
                    logical_bytes=module.TOTAL, physical_bytes=module.TOTAL,
                    current_unmounted_filesystem=True, device_writes=False, capture=str(supplied),
                    serial='synthetic-fixture', device='/dev/block/fake')
    receipt.write_text(json.dumps(metadata))
    assert len(module.supplied_capture(supplied, receipt)[3]) == 3
    metadata['current_unmounted_filesystem'] = False
    receipt.write_text(json.dumps(metadata))
    try:
        module.supplied_capture(supplied, receipt)
        raise AssertionError('mounted-source receipt accepted')
    except RuntimeError:
        pass
print('PASS: host-only current-source receipt/bounds/remapping preserve outside OS; not device image verification')
