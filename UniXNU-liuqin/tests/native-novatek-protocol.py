#!/usr/bin/env python3
"""Host tests of the independent parser/decoder; not device load/input proof."""
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]
if len(sys.argv) != 2:
    raise SystemExit('usage: native-novatek-protocol.py LOCAL_PANEL_FIRMWARE.bin')
firmware = Path(sys.argv[1]).resolve()
test = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "PongNovatekProtocol.h"
using namespace ponginput;
static void put32(std::vector<uint8_t> &v,unsigned at,uint32_t x) {
    for(unsigned i=0;i<4;i++)v[at+i]=uint8_t(x>>(8*i));
}
static void checksum(uint8_t *v) {
    unsigned sum=0;for(unsigned i=0;i<64;i++)sum+=v[i];v[64]=uint8_t(-sum);
}
int main(int argc,char **argv) {
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);
    const std::vector<uint8_t> data{std::istreambuf_iterator<char>(input),{}};
    FirmwarePlan p;
    assert(nvtFirmwarePlan(data.data(),data.size(),p));
    assert(p.count==16 && p.active==4 && p.total==201924 && p.version==0x19);
    assert(p.sections[0].bytes==119228 && p.sections[1].bytes==82184);
    assert(p.sections[14].source==0 && p.sections[15].source==0x100);
    for(unsigned i=2;i<14;i++)assert(!p.sections[i].bytes);
    assert(!nvtFirmwarePlan(nullptr,data.size(),p));
    assert(!nvtFirmwarePlan(data.data(),data.size()-1,p));
    for(unsigned at:{0u,0x20u,245759u,241664u}) {
        auto invalid=data;invalid[at]^=0xff;
        assert(!nvtFirmwarePlan(invalid.data(),invalid.size(),p));
        assert(!p.count && !p.active);
    }
    for(auto [at,value]:{std::pair{8u,0xffffffffu},{4u,0x1fb000u},{0x1f8u,245759u},{0x1f0u,0x114108u}}) {
        auto invalid=data;put32(invalid,at,value);
        assert(!nvtFirmwarePlan(invalid.data(),invalid.size(),p));
    }
    auto overlay=data;overlay[0x28]|=0x10;assert(!nvtFirmwarePlan(overlay.data(),overlay.size(),p));
    const unsigned mx=NvtSensorMaxX,my=NvtSensorMaxY;
    uint8_t info[38]={0x19,0xe6,38,60,0x46,0x50,0x70,0x80};unsigned x=0,y=0;
    assert(nvtGeometry(info,sizeof(info),0x19,x,y) && x==18000 && y==28800); // actual device50 FWINFO regression
    info[1]^=1;assert(!nvtGeometry(info,sizeof(info),0x19,x,y));info[1]^=1;
    info[4]=0x07;info[5]=0x08;assert(!nvtGeometry(info,sizeof(info),0x19,x,y)); // do not accept display pixels instead
    uint8_t report[NvtReportBytes]={};NativeReport result;
    assert(nvtReport(report,sizeof(report),mx,my,result) && !result.count);
    report[0]=1<<3|1;report[1]=0x27;report[2]=0x10;report[3]=0x4e;report[4]=0x20;
    report[5]=0x34;report[98]=0x12;checksum(report);
    assert(nvtReport(report,sizeof(report),mx,my,result));
    assert(result.count==1 && result.contacts[0].id==1 && result.contacts[0].x==10000 && result.contacts[0].y==20000);
    assert(result.contacts[0].pressure==0x12); // tail pressure, NOT width or legacy12-bit encoding
    report[6]=report[0];checksum(report);assert(!nvtReport(report,sizeof(report),mx,my,result)); // duplicate id
    report[6]=0;checksum(report);report[64]^=1;assert(!nvtReport(report,sizeof(report),mx,my,result));
    checksum(report);assert(!nvtReport(report,sizeof(report),999,my,result));
    report[1]=0x46;report[2]=0x51;checksum(report);assert(!nvtReport(report,sizeof(report),mx,my,result)); // beyond18000
    assert(!nvtReport(report,65,mx,my,result));
    uint8_t startup[NvtReportBytes]={};for(unsigned i=0;i<6;i++)startup[i]=0x77;
    assert(nvtReportResult(startup,sizeof(startup),mx,my,result)==ReportStartup);
    assert(nvtReportResult(report,65,mx,my,result)==ReportLength);
    assert(nvtReportResult(report,sizeof(report),1800,2880,result)==ReportGeometry);
    checksum(report);assert(nvtReportResult(report,sizeof(report),mx,my,result)==ReportCoordinate);
    report[64]^=1;assert(nvtReportResult(report,sizeof(report),mx,my,result)==ReportChecksum);
    uint8_t packet[108]={};NativePen pen;
    packet[65]=0xff;assert(nvtPen(packet,sizeof(packet),mx,my,pen)==0);
    packet[65]=0xf0;assert(nvtPen(packet,sizeof(packet),mx,my,pen)==2);
    packet[65]=1;packet[66]=0x27;packet[67]=0x10;packet[68]=0x4e;packet[69]=0x20;
    packet[70]=0x0a;packet[71]=0xbc;packet[72]=uint8_t(-20);packet[73]=30;packet[75]=1;
    assert(nvtPen(packet,sizeof(packet),mx,my,pen)==1);
    assert(pen.x==10000 && pen.y==20000 && pen.pressure==0xabc && pen.touch && pen.range && pen.tiltX==-20 && pen.tiltY==30 && pen.tiltValid);
    packet[70]=0x10;assert(nvtPen(packet,sizeof(packet),mx,my,pen)==-1);
    assert(nvtPenResult(packet,sizeof(packet),mx,my,pen)==PenPressure && !pen.touch);
    packet[70]=0;packet[71]=0;assert(nvtPen(packet,sizeof(packet),mx,my,pen)==1 && pen.range && !pen.touch);
    packet[70]=3;packet[71]=0x2e; // observed55 pressure814, valid contact
    for(unsigned aux:{61u,65u,127u,128u,255u}) {
        packet[72]=uint8_t(aux);packet[73]=uint8_t(255-aux);
        assert(nvtPenResult(packet,sizeof(packet),mx,my,pen)==PenData && pen.range && pen.touch && pen.pressure==814);
        assert(pen.tiltX==int(int8_t(aux)) && pen.tiltY==int(int8_t(255-aux)));
        assert(pen.tiltValid==(pen.tiltX>=-90 && pen.tiltX<=90 && pen.tiltY>=-90 && pen.tiltY<=90));
    }
    assert(nvtPen(packet,78,mx,my,pen)==-1);
    assert(nvtPenResult(packet,78,mx,my,pen)==PenLength);
    packet[65]=2;assert(nvtPenResult(packet,sizeof(packet),mx,my,pen)==PenFormat);
    packet[65]=1;packet[66]=0xff;assert(nvtPenResult(packet,sizeof(packet),mx,my,pen)==PenCoordinate);
    assert(nvtPenResult(packet,sizeof(packet),1800,2880,pen)==PenGeometry);
    puts("PASS: host-only device50 FWINFO/high-resolution BE16 touch/pen and tail-pressure regressions, checksum/bounds; NOT hardware input evidence");
}
'''
with tempfile.TemporaryDirectory(prefix='native-nvt-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    cpp = temp / 'test.cpp'
    cpp.write_text(test)
    subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(repo / 'drivers/PongInputNative'), str(cpp), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test'), str(firmware)], check=True)
