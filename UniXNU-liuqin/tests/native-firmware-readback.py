#!/usr/bin/env python3
"""Test actual bounded preboot SRAM verifier with mocked reads; no device proof."""
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]
if len(sys.argv) != 2:
    raise SystemExit('usage: native-firmware-readback.py LOCAL_PANEL_FIRMWARE.bin')
source = (repo / 'drivers/PongInputNative/PongInputNative.cpp').read_text()
start = source.index('bool verifyFirmwareBytes()')
brace = source.index('{', start)
depth = 0
for end in range(brace, len(source)):
    depth += (source[end] == '{') - (source[end] == '}')
    if not depth:
        method = source[start:end+1]
        break
test = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "PongNovatekProtocol.h"
using namespace ponginput;
static const uint8_t *nativeFirmware;
enum ProbeState {Pass,Fail,Running};
static void PongProbeRecord(unsigned,const char *,ProbeState,const char *,uint64_t=0,uint64_t=0) {}
static void IOLog(const char *,...) {}
struct Controller {
    FirmwarePlan firmwarePlan;unsigned reads=0,bytesRead=0;
    bool mismatch=false,ioFailure=false;
    bool readNovatek(uint32_t address,uint8_t *data,unsigned bytes) {
        assert(bytes && bytes<=256);++reads;bytesRead+=bytes;
        if(ioFailure)return false;
        for(unsigned k=0;k<firmwarePlan.count;k++) {
            const auto &s=firmwarePlan.sections[k];
            if(s.bytes && address>=s.destination && address-s.destination<=s.bytes && bytes<=s.bytes-(address-s.destination)) {
                memcpy(data,nativeFirmware+s.source+address-s.destination,bytes);
                if(mismatch)data[0]^=1;
                return true;
            }
        }
        assert(false && "read outside an active SRAM section");return false;
    }
'''
main = r'''
};
int main(int argc,char **argv) {
    assert(argc==2);std::ifstream input(argv[1],std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};nativeFirmware=bytes.data();
    Controller d;assert(nvtFirmwarePlan(nativeFirmware,bytes.size(),d.firmwarePlan));
    assert(d.verifyFirmwareBytes() && d.bytesRead==201924 && d.reads==790);
    Controller bad;bad.firmwarePlan=d.firmwarePlan;bad.mismatch=true;
    assert(!bad.verifyFirmwareBytes() && bad.reads==1);
    Controller failed;failed.firmwarePlan=d.firmwarePlan;failed.ioFailure=true;
    assert(!failed.verifyFirmwareBytes() && failed.reads==1);
    puts("PASS: host-only actual preboot verifier covers all201924B, fails immediately on mismatch/I/O error; NOT hardware readback evidence");
}
'''
with tempfile.TemporaryDirectory(prefix='native-fw-check-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    cpp = temp / 'test.cpp'
    cpp.write_text(test + method + main)
    subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I',
                    str(repo / 'drivers/PongInputNative'), str(cpp), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test'), str(Path(sys.argv[1]).resolve())], check=True)
