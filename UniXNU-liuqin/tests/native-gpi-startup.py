#!/usr/bin/env python3
"""Host-only mocked MMIO tests of actual native GPI methods; not DMA proof."""
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
source = (repo / 'drivers/PongInputNative/PongInputNative.cpp').read_text()


def extract(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[start:end + 1]
    raise AssertionError(f'unterminated {signature}')


irq = extract('static void interrupt(void *cookie)')
assert 'IOLog(' not in irq and 'PongProbeRecord(' not in irq
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>
#include "PongInputNativeBoard.h"
using namespace ponginput;
enum ProbeState {Pass,Fail,Running};
static void PongProbeRecord(unsigned,const char *,ProbeState,const char *,uint64_t=0,uint64_t=0) {}
static void IOLog(const char *,...) {}
using IOReturn=int;
static IOReturn attachResult=0;
static IOReturn armcpu_spi_attach(uint32_t,bool,void (*)(void *),void *,uint32_t *,uint64_t *) {return attachResult;}
struct MockWindow {
    std::map<unsigned,uint32_t> values;
    std::vector<std::tuple<bool,unsigned,uint32_t>> accesses;
    uint32_t read(unsigned off) {auto v=values[off];accesses.emplace_back(false,off,v);return v;}
    void write(unsigned off,uint32_t value) {
        accesses.emplace_back(true,off,value);values[off]=value;
        for(auto [clear,bit]: {std::pair{0x230a0u,1u}, {0x230a4u,2u}, {0x230c0u,8u}})
            if(off==control(1,clear))values[control(1,0x23080)]&=~bit;
    }
};
struct Controller {
'''
fields = r'''
    MockWindow gpi;
    unsigned instance=1,eventCredit=EventEntries-1,error=0,channelAck[2]={},eventAck=0,eventNotifications=0;
    unsigned irqFailureState=0,activeCommand=0x101;
    uint64_t irqCount=0;
    bool gpiIrqAttached=false,started[2]={};
    int failCommand=-1;
    std::vector<std::tuple<bool,unsigned,unsigned,unsigned>> commands;
    unsigned c(unsigned off) const {return control(instance,off);}
    bool command(bool ev,unsigned ch,unsigned opcode,unsigned expected) {
        commands.emplace_back(ev,ch,opcode,expected);
        if(opcode==1)for(unsigned k=0;k<2;k++) {
            assert(gpi.values.at(channel(instance,k)+8)==DmaIova+(k?RxRingOff:TxRingOff));
            assert(gpi.values.at(channel(instance,k)+0x60)==((unsigned(k==0)<<16)|(GpiSpiProtocol<<4)|SeId));
        }
        return int(commands.size())-1!=failCommand;
    }
'''
test = r'''
};
int main() {
    Controller d;
    assert(d.startGpi());
    const std::vector<std::tuple<bool,unsigned,unsigned,unsigned>> expected={
        {true,0,0,1},{false,0,0,1},{false,1,0,1},{false,0,1,2},{false,1,1,2}};
    assert(d.commands==expected && d.started[0] && d.started[1]);
    for(int failure=0;failure<5;failure++) {
        Controller f;f.failCommand=failure;
        assert(!f.startGpi() && f.commands.size()==size_t(failure+1));
        assert(f.started[0]==(failure==4) && !f.started[1]);
    }
    attachResult=1;Controller rejected;
    assert(!rejected.startGpi() && !rejected.gpiIrqAttached && rejected.commands.empty());attachResult=0;
    for(unsigned type: {4u,64u,5u,16u}) {
        Controller f;auto &w=f.gpi;
        w.values[control(1,0x23080)]=type;
        w.values[control(1,0x23100)]=0x123;
        w.values[control(1,0x23118)]=0x456;
        w.values[control(1,0x23200)]=0xabcdef;
        w.values[control(1,0x23090)]=1;
        w.values[channel(1,0)]=0x1010000a;w.values[channel(1,1)]=0x10100002;
        w.values[event(1)]=0x10110002;
        Controller::interrupt(&f);
        assert(f.irqFailureState==2 && f.error==type && f.irqCount==1);
        assert(f.irqFailure.global==0x123 && f.irqFailure.general==0x456 && f.irqFailure.log==0xabcdef);
        assert(f.irqFailure.tx==0x1010000a && f.irqFailure.rx==0x10100002 && f.irqFailure.command==0x101);
        assert(f.channelAck[0]==0 && f.eventAck==0 && f.eventNotifications==0);
        bool sawLog=false,sawGeneral=false,sawGlobal=false;
        for(auto [write,off,value]:w.accesses) {
            (void)value;
            if(write)assert(sawLog && sawGeneral && sawGlobal);
            else {sawLog|=off==control(1,0x23200);sawGeneral|=off==control(1,0x23118);sawGlobal|=off==control(1,0x23100);}
        }
        w.values[control(1,0x23200)]=7;Controller::interrupt(&f);
        assert(f.irqFailure.log==0xabcdef && f.irqFailure.count==1); // immutable first error
    }
    Controller normal;
    normal.gpi.values[control(1,0x23080)]=11;
    normal.gpi.values[control(1,0x23090)]=3;
    Controller::interrupt(&normal);
    assert(normal.channelAck[0]==1 && normal.channelAck[1]==1 && normal.eventAck==1 && normal.eventNotifications==1);
    assert(!normal.error && !normal.irqFailureState);
    puts("PASS: host-only GPI pair ordering, failure exits, before-clear capture and fatal ACK suppression; NOT hardware/DMA evidence");
}
'''
text = harness + extract('struct IrqFailure') + ' irqFailure;\n' + fields + extract('bool startGpi()') + '\n' + irq + '\n' + test
with tempfile.TemporaryDirectory(prefix='native-gpi-', dir='/private/var/folders/6w/pxwvg93n2156nbcs9f4gjf540000gn/T/opencode') as temp:
    temp = Path(temp)
    cpp = temp / 'test.cpp'
    cpp.write_text(text)
    subprocess.run(['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(repo / 'drivers/PongInputNative'), str(cpp), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
