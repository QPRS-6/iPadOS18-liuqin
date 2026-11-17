#!/usr/bin/env python3
"""Read liuqin's FAT16 NON-HLOS, retain signed ADSP pieces, assemble PAS input.
No firmware hashes, device writes, calibration or firmware in the repository.
"""
import argparse
import json
import re
import struct
from pathlib import Path


class Fat16:
    def __init__(self, path):
        self.file = path.open('rb')
        self.size = path.stat().st_size
        h = self.read(0, 512)
        sector = struct.unpack_from('<H', h, 11)[0]
        clusters = h[13]
        reserved = struct.unpack_from('<H', h, 14)[0]
        copies = h[16]
        entries = struct.unpack_from('<H', h, 17)[0]
        sectors = struct.unpack_from('<H', h, 22)[0]
        if sector not in (512, 1024, 2048, 4096) or not clusters or not sectors or not copies:
            raise ValueError('Unsupported FAT16 geometry')
        self.cluster_bytes = sector * clusters
        self.fat = self.read(reserved * sector, sectors * sector)
        self.root = (reserved + copies * sectors) * sector
        self.root_bytes = entries * 32
        self.data = self.root + ((self.root_bytes + sector - 1) // sector) * sector

    def read(self, offset, length):
        if offset < 0 or length > self.size - offset:
            raise ValueError('Required allocated data lies outside NON-HLOS')
        self.file.seek(offset)
        data = self.file.read(length)
        if len(data) != length:
            raise ValueError('Short read')
        return data

    def chain(self, cluster):
        data, seen = bytearray(), set()
        while cluster < 0xfff8:
            if cluster < 2 or cluster * 2 + 2 > len(self.fat) or cluster in seen:
                raise ValueError('Invalid/cyclic FAT chain')
            if len(data) + self.cluster_bytes > 64 * 1024 * 1024:
                raise ValueError('Entry exceeds firmware/directory bound')
            seen.add(cluster)
            data += self.read(self.data + (cluster - 2) * self.cluster_bytes, self.cluster_bytes)
            cluster = struct.unpack_from('<H', self.fat, cluster * 2)[0]
        return data

    def entries(self, raw):
        parts = {}
        for at in range(0, len(raw), 32):
            entry = raw[at:at + 32]
            if len(entry) != 32 or not entry[0]:
                return
            if entry[0] == 0xe5:
                parts = {}
                continue
            if entry[11] == 15:
                chars = entry[1:11] + entry[14:26] + entry[28:32]
                parts[entry[0] & 31] = chars.decode('utf-16le').split('\0')[0].replace('\uffff', '')
                continue
            stem, ext = entry[:8].decode('ascii').strip(), entry[8:11].decode('ascii').strip()
            name = ''.join(parts[k] for k in sorted(parts)) if parts else stem + ('.' + ext if ext else '')
            parts = {}
            if name in ('.', '..') or entry[11] & 8:
                continue
            if '/' in name or '\\' in name:
                raise ValueError('Invalid directory entry name')
            yield name.lower(), entry[11], struct.unpack_from('<H', entry, 26)[0], struct.unpack_from('<I', entry, 28)[0]

    def image_files(self):
        root = list(self.entries(self.read(self.root, self.root_bytes)))
        images = [e for e in root if e[0] == 'image' and e[1] & 16]
        if len(images) != 1:
            raise ValueError('Expected one image directory')
        return list(self.entries(self.chain(images[0][2])))


def assemble(files):
    headers = files['adsp.mdt']
    if headers[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected little-endian ELF32 ADSP metadata')
    phoff = struct.unpack_from('<I', headers, 28)[0]
    entry_size, count = struct.unpack_from('<HH', headers, 42)
    if entry_size != 32 or not 1 <= count <= 64 or phoff + count * 32 > len(headers):
        raise ValueError('Invalid ADSP program headers')
    segments, end, missing = [], 0, []
    for i in range(count):
        kind, offset, va, pa, size, mem, flags, alignment = struct.unpack_from('<8I', headers, phoff + 32 * i)
        name = f'adsp.b{i:02d}'
        if size and name not in files:
            missing.append(name)
        if size and name in files and len(files[name]) != size:
            raise ValueError(f'{name}: piece size does not match program header')
        if offset + size > 64 * 1024 * 1024:
            raise ValueError('ADSP file exceeds 64 MiB loader bound')
        if size:
            end = max(end, offset + size)
        segments.append(dict(index=i, type=kind, offset=offset, address=pa,
            file_bytes=size, memory_bytes=mem, flags=flags))
    if missing:
        raise ValueError('Missing signed pieces: ' + ', '.join(missing))
    output = bytearray(end)
    spans = []
    for s in segments:
        size, offset = s['file_bytes'], s['offset']
        if not size:
            continue
        if any(offset < b and a < offset + size for a, b in spans):
            raise ValueError('Overlapping firmware segments')
        spans.append((offset, offset + size))
        output[offset:offset + size] = files[f"adsp.b{s['index']:02d}"]
    loads = [s for s in segments if s['type'] == 1 and s['memory_bytes'] and
        (s['flags'] & 0x07000000) != 0x02000000]
    if not loads:
        raise ValueError('No loadable ADSP segments')
    low = min(s['address'] for s in loads)
    high = max(s['address'] + s['memory_bytes'] for s in loads)
    return output, dict(segments=segments, load_min=low, load_max=high,
        relocated_span=((high + 4095) & ~4095) - low, file_bytes=len(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--non-hlos', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.exists():
        raise SystemExit('Refusing to overwrite an existing extraction')
    fat = Fat16(args.non_hlos)
    files = {}
    try:
        for name, attrs, cluster, length in fat.image_files():
            if attrs & 16 or not (re.fullmatch(r'adsp\.(mdt|b\d\d)', name) or name in ('battmgr.jsn', 'adspr.jsn', 'adspua.jsn')):
                continue
            data = bytes(fat.chain(cluster))
            if length > len(data):
                raise ValueError('Short firmware cluster chain')
            files[name] = data[:length]
        combined, report = assemble(files)
    finally:
        fat.file.close()
    args.output_dir.mkdir(parents=True)
    for name, data in files.items():
        (args.output_dir / name).write_bytes(data)
    (args.output_dir / 'adsp.mbn').write_bytes(combined)
    report.update(input=str(args.non_hlos.resolve()), board='liuqin',
        rom='OS3.0.7.0.VMYCNXM (source confirmed by owner)',
        signature_verification='PAS required on device; not claimed by extraction')
    (args.output_dir / 'extraction.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"ADSP: {len(files)} files, {report['file_bytes']} bytes, relocation span {report['relocated_span']} bytes")
    print(args.output_dir / 'adsp.mbn')


if __name__ == '__main__':
    main()
