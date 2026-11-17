#!/usr/bin/env python3
"""Execute the real console flush helper with a recording writer; no storage/device."""
import os,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'drivers/NativeUfs/PongUfsTransport.cpp').read_text()
helper=source[source.index('static void pongLogFlush(void)'):source.index('\nstatic void pongLogThread')]
fixture=r'''
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cassert>
#include "PongLogPolicy.h"
enum : uint32_t {kPongLogBytes=4u*1024*1024-4096};
static uint8_t gPongLogRing[kPongLogBytes];
static uint32_t gPongLogEnabled=1,gPongLogWritten=0,gPongLogFlushes=0,gPongLogHead=0,gPongLogFlushBudget=240;
static uint64_t gPongLogLba=100;
static struct {bool ready=true,writable=true,poisoned=false;} gHost;
using clock_sec_t=uint32_t;using clock_usec_t=uint32_t;
static void clock_get_system_microtime(clock_sec_t *s,clock_usec_t *u){*s=1;*u=0;}
static void pongLogPut32(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static void pongLogPut64(uint8_t *p,uint64_t v){memcpy(p,&v,8);}
static unsigned writes=0;static bool allow=true;
static bool pongLogWrite(uint64_t lba,const uint8_t *,uint32_t bytes){assert(lba>=100 && bytes%4096==0 && bytes<=kPongLogBytes);writes++;return allow;}
'''
checks=r'''
int main(){
 for(uint32_t invalid:{0u,721u,UINT32_MAX})assert(pongLogFlushBudget(invalid)==240);
 assert(pongLogFlushBudget(720)==720 && pongLogFlushBudget(1)==1);
 gPongLogHead=4096;gPongLogFlushes=240;pongLogFlush();assert(writes==0);
 gPongLogFlushBudget=pongLogFlushBudget(720);pongLogFlush();assert(writes==2 && gPongLogFlushes==241);
 const auto before=writes;pongLogFlush();assert(writes==before); // unchanged snapshots do not consume budget
 gPongLogHead=8192;allow=false;pongLogFlush();assert(gPongLogFlushes==241 && gPongLogWritten==4096);
 allow=true;pongLogFlush();assert(gPongLogFlushes==242 && gPongLogWritten==8192);
 gPongLogFlushes=720;gPongLogHead=12288;const auto stopped=writes;pongLogFlush();assert(writes==stopped);
 gPongLogFlushes=242;gHost.poisoned=true;pongLogFlush();assert(writes==stopped);
 puts("PASS old240 limit, explicit720 continuation, unchanged/failure snapshots, final finite cap and poisoned transport refusal");
}
'''
fixture=fixture.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
with tempfile.TemporaryDirectory(prefix='log-budget-',dir=os.environ.get('TMPDIR')) as tmp:
 tmp=Path(tmp);file=tmp/'test.cpp';file.write_text(fixture+helper+checks);binary=tmp/'test'
 cmd=['xcrun','clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-I'+str(root/'drivers/NativeUfs'),str(file),'-o',str(binary)]
 subprocess.run(cmd,check=True,timeout=30);subprocess.run([binary],check=True,timeout=15)
