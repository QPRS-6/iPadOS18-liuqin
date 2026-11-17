#!/usr/bin/env python3
"""Host-only pen status-read/report-page restoration regression."""
from pathlib import Path
import subprocess
import tempfile

repo=Path(__file__).resolve().parents[1]
source=(repo/'drivers/PongInputNative/PongInputNative.cpp').read_text()
start=source.index('bool penStatusSnapshot()');brace=source.index('{',start);depth=0
for end in range(brace,len(source)):
    depth+=(source[end]=='{')-(source[end]=='}')
    if not depth:
        method=source[start:end+1]
        break
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "PongInputNativeBoard.h"
#include "PongNovatekProtocol.h"
using namespace ponginput;
enum ProbeState {Fail};
static void PongProbeRecord(unsigned,const char *,ProbeState,const char *,uint64_t=0,uint64_t=0) {}
static void IOLog(const char *,...) {}
struct Window {uint32_t read(unsigned){return 0;}};
struct Controller {
    Window tlmm;uint32_t lastReportAck=0;bool fail=false,restoreFail=false,page=false;
    unsigned restores=0;std::vector<uint32_t> addresses;
    bool readNovatek(uint32_t address,uint8_t *data,unsigned bytes) {
        page=false;addresses.push_back(address);for(unsigned i=0;i<bytes;i++)data[i]=0;return !fail;
    }
    bool selectReportPage(){++restores;page=!restoreFail;return page;}
'''
suffix=r'''
};
int main() {
    Controller good;assert(good.penStatusSnapshot());
    const std::vector<uint32_t> expected={NvtHost,NvtResetState,NvtFwInfo};
    assert(good.addresses==expected && good.restores==1 && good.page);
    Controller io;io.fail=true;assert(!io.penStatusSnapshot() && io.addresses.size()==1 && io.restores==1);
    Controller page;page.restoreFail=true;assert(!page.penStatusSnapshot() && !page.page && page.restores==1);
    puts("PASS: pen status reads use only known addresses and restore report page on success/failure; failed restore never permits report continuation; HOST MOCK ONLY");
}
'''
with tempfile.TemporaryDirectory(prefix='native-pen-status-',dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp=Path(temp);cpp=temp/'test.cpp';cpp.write_text(prefix+method+suffix)
    subprocess.run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(repo/'drivers/PongInputNative'),str(cpp),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
