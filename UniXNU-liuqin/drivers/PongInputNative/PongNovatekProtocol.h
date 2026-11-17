/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_NOVATEK_PROTOCOL_H
#define PONG_NOVATEK_PROTOCOL_H
#include <stdint.h>
namespace ponginput {
constexpr uint32_t NvtEvent=0x125800, NvtReset=0x1fb43e, NvtBootReady=0x1fb50d;
constexpr uint32_t NvtAutoCopy=0x1fc925, NvtCrcFlags=0x1fb533;
/* liuqin firmware19/e6 reports sensor units, not display pixels. Device50
 * FWINFO reports18000/28800; vendor reads BE16 finger/pen coordinates. */
constexpr unsigned NvtSensorMaxX=18000, NvtSensorMaxY=28800, NvtReportBytes=108;
/* Host offset50 becomes wire opcode d0; it is NOT address event+d0. */
constexpr uint32_t NvtHost=NvtEvent+0x50, NvtResetState=NvtEvent+0x60, NvtFwInfo=NvtEvent+0x78;
struct FirmwareSection {uint32_t source=0,destination=0,bytes=0,crc=0;};
struct FirmwarePlan {FirmwareSection sections[32];unsigned count=0,active=0,total=0;uint8_t version=0;};
static inline uint32_t nvtLe32(const uint8_t *p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static inline unsigned nvtBe16(const uint8_t *p){return (unsigned(p[0])<<8)|p[1];}
static inline bool nvtGeometry(const uint8_t *info,unsigned bytes,uint8_t version,unsigned &x,unsigned &y) {
    x=y=0;
    if(!info || bytes<8 || unsigned(info[0])+info[1]!=255 || info[0]!=version || info[2]!=38 || info[3]!=60)return false;
    const unsigned rawX=nvtBe16(info+4),rawY=nvtBe16(info+6);
    if(rawX!=NvtSensorMaxX || rawY!=NvtSensorMaxY)return false;
    x=rawX;y=rawY;return true;
}
static inline bool nvtFirmwarePlan(const uint8_t *data,unsigned bytes,FirmwarePlan &result) {
    result=FirmwarePlan{};
    /* This loader implements the liuqin cascade container only. Unknown
     * shapes/overlays cannot silently become hardware writes. */
    if(!data || bytes!=245760 || nvtLe32(data)!=0x200 || !(data[0x20]&2) || (data[0x28]&0x10))return false;
    if(data[bytes-3]!='N' || data[bytes-2]!='V' || data[bytes-1]!='T')return false;
    if(unsigned(data[bytes-4096])+data[bytes-4095]!=255)return false;
    FirmwarePlan plan;plan.version=data[bytes-4096];plan.count=16;
    bool nextHeader=false;
    for(unsigned k=0;k<plan.count;k++) {
        FirmwareSection &s=plan.sections[k];uint32_t raw=0;
        if(k<2) {
            s.source=nvtLe32(data+12*k);s.destination=nvtLe32(data+12*k+4);
            raw=nvtLe32(data+12*k+8);s.crc=nvtLe32(data+0x18+4*k);
            if(!raw)return false;
        } else {
            unsigned at=nextHeader?0x1f0:0x30+(k-2)*16;
            s.destination=nvtLe32(data+at);raw=nvtLe32(data+at+4);
            s.source=nvtLe32(data+at+8);s.crc=nvtLe32(data+at+12);
            if(raw && s.source<0x200)nextHeader=true;
        }
        if(s.source>bytes)return false;
        if(!raw)continue; // reserved partition, not a one-byte download
        if(raw>=bytes || raw+1>bytes-s.source)return false;
        s.bytes=raw+1;
        bool low=s.destination<0x20000 && s.bytes<=0x20000-s.destination;
        bool high=s.destination>=0x100000 && s.destination<0x120000 && s.bytes<=0x120000-s.destination;
        if(!low && !high)return false; // never download into Novatek MMIO/event space
        for(unsigned j=0;j<k;j++) {
            const FirmwareSection &p=plan.sections[j];
            if(p.bytes && s.destination<p.destination+p.bytes && p.destination<s.destination+s.bytes)return false;
        }
        ++plan.active;plan.total+=s.bytes;
    }
    if(!nextHeader || plan.active!=4 || plan.sections[0].destination!=0 || plan.sections[1].destination!=0x100000 ||
       plan.sections[14].destination!=0x114108 || plan.sections[15].destination!=0x114208)return false;
    result=plan;return true;
}
struct NativeContact {unsigned id=0,x=0,y=0,pressure=0;};
struct NativeReport {NativeContact contacts[10];unsigned count=0;};
struct NativePen {unsigned range=0,touch=0,x=0,y=0,pressure=0,distance=0;int tiltX=0,tiltY=0;bool tiltValid=false;};
/* Vendor reads109 bytes including index; pen format starts at data byte65.
 * Vendor checksum option is OFF for pen: do not invent a checksum byte or
 * borrow unverified buttons/battery/authentication semantics. */
enum PenResult : int {PenOut=0,PenData=1,PenId=2,PenLength=-1,PenGeometry=-2,PenFormat=-3,PenCoordinate=-4,PenPressure=-5,PenTilt=-6};
static inline PenResult nvtPenResult(const uint8_t *data,unsigned bytes,unsigned maxX,unsigned maxY,NativePen &result) {
    result=NativePen{};
    if(!data || bytes!=NvtReportBytes)return PenLength;
    if(maxX!=NvtSensorMaxX || maxY!=NvtSensorMaxY)return PenGeometry;
    const uint8_t *p=data+65;
    if(p[0]==0xff)return PenOut;
    if(p[0]==0xf0)return PenId; // ID packet, not a new position or a release
    if(p[0]!=1)return PenFormat;
    NativePen pen;
    pen.x=nvtBe16(p+1);pen.y=nvtBe16(p+3);
    pen.pressure=nvtBe16(p+5);pen.distance=nvtBe16(p+9);
    pen.tiltX=int(int8_t(p[7]));pen.tiltY=int(int8_t(p[8]));
    /* Signed plane angles may exceed the vendor's +/-60 advertised axes.
     * Mark implausible orientation missing instead of losing valid tip/XY. */
    pen.tiltValid=pen.tiltX>=-90 && pen.tiltX<=90 && pen.tiltY>=-90 && pen.tiltY<=90;
    if(pen.x>maxX || pen.y>maxY)return PenCoordinate;
    if(pen.pressure>4095)return PenPressure;
    pen.touch=pen.pressure!=0;pen.range=pen.touch || pen.distance!=0;
    result=pen;return PenData;
}
static inline int nvtPen(const uint8_t *data,unsigned bytes,unsigned maxX,unsigned maxY,NativePen &result) {
    const PenResult status=nvtPenResult(data,bytes,maxX,maxY,result);
    return status<0?-1:int(status); // existing accepted/rejected contract retained
}
enum ReportResult : unsigned {ReportOkay=0,ReportLength,ReportGeometry,ReportChecksum,ReportId,ReportDuplicate,ReportCoordinate,ReportStartup};
static inline bool nvtStartupPacket(const uint8_t *data,unsigned bytes) {
    if(!data || bytes<6)return false;
    for(unsigned i=0;i<6;i++)if(data[i]!=0x77)return false;
    return true; // vendor checks this startup token BEFORE report checksum
}
static inline ReportResult nvtReportResult(const uint8_t *data,unsigned bytes,unsigned maxX,unsigned maxY,NativeReport &result) {
    result=NativeReport{};
    if(!data || bytes!=NvtReportBytes)return ReportLength;
    if(maxX!=NvtSensorMaxX || maxY!=NvtSensorMaxY)return ReportGeometry;
    if(nvtStartupPacket(data,bytes))return ReportStartup;
    unsigned sum=0;for(unsigned i=0;i<65;i++)sum+=data[i];
    if(sum&255)return ReportChecksum;
    NativeReport report;unsigned ids=0;
    for(unsigned k=0;k<10;k++) {
        const uint8_t *p=data+6*k;unsigned status=p[0]&7;
        if(status!=1 && status!=2)continue;
        /* Vendor work_func6bbc/6bc0 and6bdc decode two BE16 coordinates;
         * index99+i includes the SPI index, thus pressure=data98+i. */
        unsigned id=p[0]>>3,x=nvtBe16(p+1),y=nvtBe16(p+3);
        if(!id || id>10)return ReportId;
        if(ids&(1u<<id))return ReportDuplicate;
        if(x>maxX || y>maxY)return ReportCoordinate;
        ids|=1u<<id;
        unsigned pressure=data[98+k]; // width is p[5], never reinterpret it as pressure
        report.contacts[report.count++]={id,x,y,pressure};
    }
    result=report;return ReportOkay;
}
static inline bool nvtReport(const uint8_t *data,unsigned bytes,unsigned maxX,unsigned maxY,NativeReport &result) {
    return nvtReportResult(data,bytes,maxX,maxY,result)==ReportOkay;
}
}
#endif
