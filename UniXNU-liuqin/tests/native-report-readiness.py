#!/usr/bin/env python3
"""Host-only tests of actual GPIO21 readiness setup and normal-state gate."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
source = (repo / 'drivers/PongInputNative/PongInputNative.cpp').read_text()


def extract(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end+1]
    raise AssertionError(signature)


test = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#include "PongInputNativeBoard.h"
#include "PongNovatekProtocol.h"
using namespace ponginput;
enum ProbeState {Pass,Fail,Skip,Running};
static void PongProbeRecord(unsigned,const char *,ProbeState,const char *,uint64_t=0,uint64_t=0) {}
static void IOLog(const char *,...) {}
static void IOSleep(unsigned) {}
struct Window {
    std::map<unsigned,uint32_t> words;
    std::vector<std::pair<unsigned,uint32_t>> writes;
    uint32_t read(unsigned off){return words[off];}
    void write(unsigned off,uint32_t value) {
        writes.emplace_back(off,value);
        // SM8475 intr_ack_high is false. Model the hardware ABI, rather
        // than incorrectly clearing status on any write as the old mock did.
        if(off==ReportCfg && !(words[off]&16) && (value&16))words[ReportStatus]=1;
        words[off]=value;
    }
};
struct Controller {
    Window tlmm;bool reportReadyOwned=false,ioFail=false,neverNormal=false;
    uint32_t reportOldCfg=0,reportWantedCfg=0;unsigned reads=0,snapshots=0;
    bool readNovatek(uint32_t address,uint8_t *data,unsigned bytes) {
        if(ioFail)return false;
        for(unsigned k=0;k<bytes;k++)data[k]=0;
        if(address==NvtResetState){data[0]=neverNormal?0xa0:uint8_t(0xa0+(reads<3?reads:3));++reads;}
        return true;
    }
    void reportSnapshot(const uint8_t *,ReportResult,bool,unsigned,unsigned){++snapshots;}
'''
main = r'''
};
int main() {
    Controller d;d.tlmm.words[ReportCtl]=0x283;d.tlmm.words[ReportCfg]=0x160;
    assert(d.prepareReportReady());
    assert(d.reportReadyOwned && d.reportWantedCfg==0x176 && !(d.tlmm.words[ReportCfg]&1));
    assert(d.tlmm.words[ReportStatus]==0);
    assert(d.tlmm.words[ReportCtl]==(0x283u&~(0x3cu|0x200u)));
    for(auto [off,value]:d.tlmm.writes){assert(off==ReportCtl || off==ReportCfg || off==ReportStatus);if(off==ReportStatus)assert(value==0);}
    assert(d.waitReportNormal() && d.reads==4 && !d.snapshots);
    Controller owned;owned.tlmm.words[ReportCfg]=1;
    assert(!owned.prepareReportReady() && !owned.reportReadyOwned && owned.tlmm.writes.empty());
    Controller early;early.neverNormal=true;
    assert(!early.waitReportNormal() && early.reads==500 && early.snapshots==0);
    Controller failed;failed.ioFail=true;assert(!failed.waitReportNormal() && !failed.reads);
    puts("PASS: host-only SM8475 ACK0/setup/ownership/normal timeout gates retained; touch raw logging disabled; NOT device readiness proof");
}
'''
with tempfile.TemporaryDirectory(prefix='native-ready-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    cpp = temp / 'test.cpp'
    cpp.write_text(test + extract('bool prepareReportReady()') + extract('bool waitReportNormal()') + main)
    subprocess.run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(repo/'drivers/PongInputNative'),
                    str(cpp),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
