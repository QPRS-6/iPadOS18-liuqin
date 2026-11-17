#!/usr/bin/env python3
"""Extract one logical window from a local Android sparse image; no hashes/readback."""
import argparse
import struct
from pathlib import Path

def extract(source, output, offset, length):
    if output.exists(): raise ValueError('Preserving existing output')
    if offset < 0 or length <= 0: raise ValueError('Invalid window')
    with source.open('rb') as src:
        header=src.read(28)
        if len(header)!=28: raise ValueError('Short sparse header')
        magic,major,minor,hdr,chunk_hdr,block,blocks,chunks,_=struct.unpack('<IHHHHIIII',header)
        if magic!=0xed26ff3a or major!=1 or hdr<28 or chunk_hdr<12 or not block or offset+length>block*blocks:
            raise ValueError('Unsupported sparse container/window')
        src.seek(hdr);position=0;end=offset+length
        with output.open('xb') as dst:
            dst.truncate(length)
            for _ in range(chunks):
                raw=src.read(chunk_hdr)
                if len(raw)!=chunk_hdr: raise ValueError('Short chunk header')
                kind,_,count,total=struct.unpack('<HHII',raw[:12])
                if total<chunk_hdr: raise ValueError('Invalid chunk size')
                data=src.tell();logical=count*block;payload=total-chunk_hdr
                lo=max(offset,position);hi=min(end,position+logical)
                if kind==0xcac1:
                    if payload!=logical: raise ValueError('Invalid raw chunk')
                    if hi>lo:
                        src.seek(data+lo-position);dst.seek(lo-offset);remaining=hi-lo
                        while remaining:
                            take=min(remaining,4<<20);buf=src.read(take)
                            if len(buf)!=take: raise ValueError('Short raw payload')
                            dst.write(buf);remaining-=take
                elif kind==0xcac2:
                    if payload!=4: raise ValueError('Invalid fill chunk')
                    fill=src.read(4)
                    if len(fill)!=4: raise ValueError('Short fill value')
                    if hi>lo:
                        dst.seek(lo-offset);remaining=hi-lo;phase=(lo-position)%4
                        while remaining:
                            take=min(remaining,4<<20)
                            buf=(fill*((take+phase+3)//4))[phase:phase+take]
                            dst.write(buf);phase=(phase+take)%4;remaining-=take
                elif kind==0xcac3:
                    if payload: raise ValueError('Invalid zero/hole chunk')
                elif kind==0xcac4:
                    if count or payload!=4: raise ValueError('Invalid checksum record')
                    # Checksum content is deliberately not checked; this is extraction.
                else: raise ValueError('Unknown sparse chunk')
                position+=logical;src.seek(data+payload)
                if position>=end:break
            if position<end:raise ValueError('Sparse input ends before window')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--offset',type=int,required=True);p.add_argument('--size',type=int,required=True)
    a=p.parse_args();extract(a.source,a.output,a.offset,a.size)
    print('Extracted local window:',a.output,flush=True)
if __name__=='__main__':main()
