#!/usr/bin/env python3
"""Actual report-loop regression: idle latch must not self-generate samples.

All time, registers and SPI data are synthetic host fixtures, not device proof.
"""
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


prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>
#include "PongInputNativeBoard.h"
#include "PongNovatekProtocol.h"
#include "PongNativePenABI.h"
using namespace ponginput;
enum ProbeState {Pass,Fail,Skip,Running};
static unsigned failureProbes=0,*errorWord=nullptr;
static uint64_t clockUs=0;
static void PongProbeRecord(unsigned,const char *,ProbeState state,const char *,uint64_t=0,uint64_t=0) {if(state==Fail)++failureProbes;}
static void IOLog(const char *,...) {}
static uint64_t nowUs(){return clockUs;}
static uint64_t mach_absolute_time(){return clockUs*1000;}
static void IOSleep(unsigned ms){clockUs+=uint64_t(ms)*1000;if(clockUs>=60100000)*errorWord=1;}
static std::vector<std::tuple<unsigned,unsigned,unsigned,unsigned>> delivered;
static bool pong_touch_device_ready(){return true;}
static bool pong_native_pen_device_ready(){return true;}
static void pong_touch_post(uint32_t type,uint32_t id,uint32_t x,uint32_t y){delivered.emplace_back(type,id,x,y);}
static void pong_native_pen_post(const pong_native_pen *){assert(false && "no pen data in touch fixture");}
struct Window {
    std::map<unsigned,uint32_t> words{{ReportCtl,1},{ReportCfg,0xe2},{ReportStatus,0},{ReportIo,0}};
    unsigned edges=0,freshFrame=0,statusStores=0;
    uint32_t read(unsigned off) {
        if(edges==0 && clockUs>=60000000){++edges;freshFrame=1;words[ReportStatus]=1;}
        if(edges==1 && clockUs>=60020000){++edges;freshFrame=2;words[ReportStatus]=1;}
        return words[off];
    }
    void write(unsigned off,uint32_t value) {
        if(off==ReportCfg && !(words[off]&16) && (value&16))words[ReportStatus]=1; // real TLMM setup may latch
        if(off==ReportStatus)++statusStores;
        words[off]=value; // ACK0 clears, WRITE1 remains pending
    }
};
struct Controller {
    Window tlmm;unsigned error=0;uint64_t irqCount=0;
    bool reportReadyOwned=false,reportPageSelected=false,touchPublished=false,penPublished=false;
    uint32_t reportOldCfg=0,reportWantedCfg=0,lastReportAck=0;NativeContact lastContacts[10];unsigned contactMask=0;NativePen lastPen;
    unsigned penHoverSamples=0,penTipSamples=0,statusSnapshots=0;
    unsigned pageWrites=0,frameReads=0,diagnosticReads=0,snapshots=0;bool failTransfer=false;
    bool waitReportNormal(){return true;}
    bool enablePen(){return true;}
    bool transfer(const uint8_t *tx,uint8_t *rx,unsigned bytes) {
        if(failTransfer)return false;
        if(!rx) {
            assert(bytes==3 && tx[0]==0xff && tx[1]==uint8_t(NvtEvent>>15) && tx[2]==uint8_t(NvtEvent>>7));
            ++pageWrites;return true;
        }
        assert(pageWrites>=1 && bytes==NvtReportBytes+2);
        for(unsigned i=0;i<bytes;i++)assert(tx[i]==0); // direct index0 burst, no per-frame page store
        memset(rx,0,bytes);rx[0]=0x12;rx[1]=0x34;
        uint8_t *data=rx+2;data[65]=0xff;
        if(!tlmm.freshFrame) {
            ++diagnosticReads;data[0]=0xff;return true; // invalid idle image never becomes input/bad READY
        }
        ++frameReads;
        if(tlmm.freshFrame==1) {
            data[0]=9;data[1]=0x27;data[2]=0x10;data[3]=0x4e;data[4]=0x20;data[98]=42;
        }
        unsigned sum=0;for(unsigned i=0;i<64;i++)sum+=data[i];data[64]=uint8_t(-sum);
        tlmm.freshFrame=0;return true;
    }
    void reportSnapshot(const uint8_t *,ReportResult,bool,unsigned,unsigned){++snapshots;}
    void tracePenPacket(const uint8_t *,PenResult,const NativePen &) {}
    void penSummary() {}
    bool penStatusSnapshot(){++statusSnapshots;reportPageSelected=false;return selectReportPage();}
'''
suffix = r'''
};
int main() {
    Controller d;errorWord=&d.error;
    assert(!d.runInput(NvtSensorMaxX,NvtSensorMaxY)); // harness terminates worker after60s
    assert(failureProbes==0 && d.statusSnapshots==2 && d.pageWrites==1+d.statusSnapshots && d.frameReads==2 && d.diagnosticReads==8);
    assert(d.tlmm.edges==2 && d.tlmm.statusStores==3 && d.tlmm.words[ReportStatus]==0 && d.lastReportAck==0);
    assert(delivered.size()==2);
    assert(std::get<0>(delivered[0])==1 && std::get<0>(delivered[1])==3);
    assert(std::get<1>(delivered[0])==0 && std::get<2>(delivered[0])==36408 && std::get<3>(delivered[0])==45510);
    assert(d.contactMask==0);
    Controller failed;failed.failTransfer=true;uint8_t bytes[NvtReportBytes]={};
    assert(!failed.selectReportPage() && !failed.reportPageSelected);
    assert(!failed.readReportBurst(bytes,sizeof(bytes)));
    puts("PASS: actual report loop survives60s idle with no fabricated READY/bad samples, delivers only2 external-edge reports, ACK0 clears each, direct SPI burst layout checked; HOST MOCK ONLY");
}
'''
methods = ('bool selectReportPage()', 'bool readReportBurst(uint8_t *data,unsigned bytes)',
           'bool prepareReportReady()', 'void releaseInput()', 'bool runInput(unsigned maxX,unsigned maxY)')
with tempfile.TemporaryDirectory(prefix='native-report-loop-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    cpp = temp / 'test.cpp'
    cpp.write_text(prefix + '\n'.join(extract(method) for method in methods) + suffix)
    subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(repo / 'drivers/PongInputNative'), str(cpp), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
