#!/usr/bin/env python3
"""Exercise actual display binding and XNU unbind code with a host scheduler fixture."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--xnu',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
header=(a.xnu/'osfmk/kern/pong_fb_affinity.h').read_text()
scheduler=(a.xnu/'osfmk/kern/sched_prim.c').read_text()
def function(source,signature):
    start=source.index(signature);brace=source.index('{',start);depth=0
    while ';' in source[start:brace]: # a multiline forward declaration
        start=source.index(signature,start+len(signature));brace=source.index('{',start)
    for end in range(brace,len(source)):
        depth+=(source[end]=='{')-(source[end]=='}')
        if not depth:return source[start:end+1]
    raise RuntimeError('Unclosed function')

# Keep binding, selection, availability, fallback and the real XNU dequeue/
# reinsert algorithm. Only EL1 identity reads and scheduler primitives are mocked.
header='\n'.join(line for line in header.splitlines() if not line.startswith(('#include','#pragma')))
header=header.replace(function(header,'static PongFBCpuSample\npongFBCpuNow'),
'''static PongFBCpuSample pongFBCpuNow(void) {
 check(updownHeld && !psetHeld && !threadHeld);
 return {me->cpu,hardwareMidr[me->cpu]^(identityMismatch?0x10u:0u),uint64_t(me->cpu)<<8};
}''')
internal=function(scheduler,'static processor_t\nthread_bind_internal(')
bind=function(scheduler,'processor_t\nthread_bind(')
unbind=function(scheduler,'void\nthread_unbind_after_queue_shutdown(')
prelude=r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>
#include <string>
#define check(v) do{if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::_Exit(1);}}while(0)
#define assert(v) check(v)
#define __assert_only
#define KERNEL_DEBUG_CONSTANT(...) ((void)0)
#define TUNABLE(type,name,arg,value) type name=value
enum {PROCESSOR_OFF_LINE=0,PROCESSOR_START=2,PROCESSOR_PENDING_OFFLINE=3,PROCESSOR_IDLE=4,PROCESSOR_DISPATCHING=5,PROCESSOR_RUNNING=6,BASEPRI_RTQUEUES=96,SCHED_TAILQ=1};
struct Pset {uint64_t cpu_bitmask=255,cpu_available_map=255,recommended_bitmask=255;};
using processor_set_t=Pset *;
struct Processor {bool processor_booted=true;int cpu_id=0;processor_set_t processor_set=nullptr;int state=PROCESSOR_RUNNING;bool processor_inshutdown=false,is_recommended=true;};
using processor_t=Processor *;
struct Thread {processor_t bound_processor=nullptr;int sched_pri=81,cpu=0;bool queued=false;unsigned refs=0,blocks=0,reinsertions=0;};
using thread_t=Thread *;
constexpr processor_t PROCESSOR_NULL=nullptr;
constexpr processor_set_t PROCESSOR_SET_NULL=nullptr;
constexpr thread_t THREAD_NULL=nullptr;
constexpr void *THREAD_CONTINUE_NULL=nullptr;
using spl_t=bool;
static std::mutex processor_updown_lock,logLock,gateLock;
static std::condition_variable gate;
static bool migrationWaiting=false,releaseMigration=false;
static bool stopMigration=false,failMigration=false,identityMismatch=false,derecommendDuringMigration=false,missingProcessor=false;
static Pset pset;
static Processor processors[8];
static uint32_t recordedMidr[8],hardwareMidr[8];
static Thread threads[4];
static thread_local Thread *me=nullptr;
static thread_local bool updownHeld=false,psetHeld=false,threadHeld=false,irqMasked=false;
static std::vector<std::string> messages;
static void lck_mtx_lock(std::mutex *m){check(!updownHeld && !psetHeld && !threadHeld);m->lock();updownHeld=true;}
static void lck_mtx_unlock(std::mutex *m){check(updownHeld && !psetHeld && !threadHeld);updownHeld=false;m->unlock();}
static spl_t splsched(){bool previous=irqMasked;irqMasked=true;return previous;}
static void splx(spl_t previous){check(!psetHeld && !threadHeld);irqMasked=previous;}
static bool ml_get_interrupts_enabled(){return !irqMasked;}
static void pset_lock(processor_set_t){check(updownHeld && irqMasked && !psetHeld);psetHeld=true;}
static void pset_unlock(processor_set_t){check(psetHeld);psetHeld=false;}
static void thread_lock(thread_t){check(updownHeld && irqMasked && !threadHeld);threadHeld=true;}
static void thread_unlock(thread_t){check(threadHeld);threadHeld=false;}
static thread_t current_thread(){check(me);return me;}
static void thread_reference(thread_t t){check(updownHeld);t->refs++;}
static processor_t cpu_to_processor(int cpu){check(cpu>=0 && cpu<8);return missingProcessor?nullptr:&processors[cpu];}
extern "C" uint32_t pong_cpu_midr(unsigned cpu){check(cpu<8);return recordedMidr[cpu];}
static void thread_assert_runq_null(thread_t t){check(threadHeld && !t->queued);}
static bool thread_run_queue_remove(thread_t t){check(threadHeld && irqMasked);bool queued=t->queued;t->queued=false;return queued;}
static void thread_run_queue_reinsert(thread_t t,int options){check(threadHeld && !t->queued && options==SCHED_TAILQ && !t->bound_processor);t->queued=true;t->reinsertions++;}
static void thread_block(void *) {
 check(updownHeld && !psetHeld && !threadHeld && !irqMasked && me->bound_processor);
 me->blocks++;
 if(stopMigration) {
  std::unique_lock<std::mutex> lock(gateLock);migrationWaiting=true;gate.notify_all();
  gate.wait(lock,[]{return releaseMigration;});
 }
 if(!failMigration)me->cpu=me->bound_processor->cpu_id;
 if(derecommendDuringMigration)me->bound_processor->is_recommended=false;
}
static void IOLog(const char *format,...) __attribute__((format(printf,1,2)));
static void IOLog(const char *format,...) {
 check(!updownHeld && !psetHeld && !threadHeld && !irqMasked);
 char text[512];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);
 std::lock_guard<std::mutex> lock(logLock);messages.emplace_back(text);
}
'''
checks=r'''
static void reset() {
 check(!updownHeld && !psetHeld && !threadHeld && !irqMasked);
 std::memset(gFBBindings,0,sizeof(gFBBindings));pset=Pset{};
 for(unsigned i=0;i<8;i++) {
  processors[i]=Processor{};processors[i].cpu_id=i;processors[i].processor_set=&pset;
  recordedMidr[i]=hardwareMidr[i]=(i>=4 && i<=6)?0x410fd470u:(i==7?0x410fd480u:0x410fd460u);
 }
 for(auto &thread:threads)thread=Thread{};
 me=&threads[0];messages.clear();gFBMiddle=1;
 migrationWaiting=releaseMigration=stopMigration=failMigration=identityMismatch=derecommendDuringMigration=missingProcessor=false;
}
static void exitCpu(unsigned cpu) {
 lck_mtx_lock(&processor_updown_lock);
 check(processors[cpu].state>=PROCESSOR_IDLE);
 pong_fb_cpu_exit(&processors[cpu]);
 processors[cpu].state=PROCESSOR_OFF_LINE;pset.cpu_available_map&=~(1ULL<<cpu);
 lck_mtx_unlock(&processor_updown_lock);
}
int main() {
 reset();unsigned layouts=0;
 for(unsigned a=0;a<6;a++)for(unsigned b=a+1;b<7;b++)for(unsigned c=b+1;c<8;c++) {
  uint32_t ids[8]={};ids[a]=0x410fd470;ids[b]=0x411fd471;ids[c]=0x41ffd47f;
  check(pongFBMiddleCpu(ids,8,0,3)==int(a));check(pongFBMiddleCpu(ids,8,1,3)==int(b));check(pongFBMiddleCpu(ids,8,2,3)==int(c));layouts++;
 }
 for(unsigned variant=0;variant<16;variant++)for(unsigned revision=0;revision<16;revision++)check(pongFBIsMiddle(0x410fd470u|(variant<<20)|revision));
 for(uint32_t id:{0u,0x510fd470u,0x410ed470u,0x410fd460u,0x410fd480u})check(!pongFBIsMiddle(id));
 check(pongFBMiddleCpu(recordedMidr,9,0,3)==-1);check(pongFBMiddleCpu(recordedMidr,8,3,3)==-1);
 for(unsigned count:{0u,1u,2u,4u,8u})check(pongFBMiddleCpu(recordedMidr,8,0,count)==-1);
 for(unsigned mode:{0u,2u}) {reset();gFBMiddle=mode;pongFBWorkerPrepare(0,3);check(!me->bound_processor && !me->refs && messages.empty());}
 reset();recordedMidr[6]=0;pongFBWorkerPrepare(0,3);check(!me->bound_processor && !me->blocks && gFBBindings[0].state==kPongFBSkipped);
 reset();recordedMidr[7]=0x410fd470;pongFBWorkerPrepare(0,3);check(!me->bound_processor && !me->blocks);
 reset();pongFBWorkerPrepare(0,2);check(!me->bound_processor && !me->blocks);
 for(unsigned fault=0;fault<11;fault++) {
  reset();auto &p=processors[4];
  switch(fault) {
   case 0:missingProcessor=true;break;
   case 1:p.processor_booted=false;break;
   case 2:p.state=PROCESSOR_OFF_LINE;break;
   case 3:p.state=PROCESSOR_START;break;
   case 4:p.state=PROCESSOR_PENDING_OFFLINE;break;
   case 5:p.processor_inshutdown=true;break;
   case 6:p.is_recommended=false;break;
   case 7:pset.recommended_bitmask&=~16ULL;break;
   case 8:pset.cpu_available_map&=~16ULL;break;
   case 9:pset.cpu_bitmask&=~16ULL;break;
   case 10:p.processor_set=nullptr;break;
  }
  pongFBWorkerPrepare(0,3);check(!me->bound_processor && !me->blocks && gFBBindings[0].state==kPongFBSkipped);
 }
 reset();me->bound_processor=&processors[7];pongFBWorkerPrepare(0,3);check(me->bound_processor==&processors[7] && !me->blocks);
 reset();me->sched_pri=BASEPRI_RTQUEUES;pongFBWorkerPrepare(0,3);check(!me->bound_processor && !me->blocks);
 for(unsigned fault=0;fault<3;fault++) {
  reset();failMigration=fault==0;identityMismatch=fault==1;derecommendDuringMigration=fault==2;
  pongFBWorkerPrepare(0,3);check(!me->bound_processor && me->blocks==1);
  check(gFBBindings[0].state==(fault==2?kPongFBSkipped:kPongFBFailed));
  pongFBWorkerPrepare(0,3);check(me->blocks==1); // failures never loop or silently rebind
 }
 reset();
 std::vector<std::thread> pool;
 for(unsigned i=0;i<3;i++)pool.emplace_back([i]{me=&threads[i];pongFBWorkerPrepare(i,3);});
 for(auto &thread:pool)thread.join();
 for(unsigned i=0;i<3;i++){check(threads[i].bound_processor==&processors[4+i]);check(threads[i].cpu==int(4+i));check(threads[i].blocks==1);}
 threads[3].bound_processor=&processors[4]; // unrelated kernel thread
 for(unsigned job=0;job<2048;job++)for(unsigned i=0;i<3;i++){me=&threads[i];pongFBWorkerPrepare(i,3);}
 check(messages.size()==9);for(unsigned i=0;i<3;i++)check(threads[i].blocks==1);
 threads[0].queued=true; // runnable: the actual XNU helper must dequeue and reinsert unbound
 exitCpu(4);check(!threads[0].bound_processor && threads[0].queued && threads[0].reinsertions==1);
 check(threads[3].bound_processor==&processors[4] && threads[1].bound_processor==&processors[5]);
 threads[0].queued=false;me=&threads[0];me->cpu=0;pongFBWorkerPrepare(0,3);check(messages.size()==10);
 // Repeated hook is harmless; dormant/running threads need no runq reinsertion.
 lck_mtx_lock(&processor_updown_lock);pong_fb_cpu_exit(&processors[4]);lck_mtx_unlock(&processor_updown_lock);
 exitCpu(5);check(!threads[1].bound_processor && !threads[1].reinsertions);
 me=&threads[2];processors[6].is_recommended=false;pongFBWorkerPrepare(2,3);check(!me->bound_processor && gFBBindings[2].state==kPongFBSkipped);
 reset();pongFBWorkerPrepare(0,3);me=&threads[3];pongFBWorkerPrepare(0,3);check(!me->bound_processor && !me->refs); // slot ownership
 reset();stopMigration=true;
 std::thread drawing([]{me=&threads[0];pongFBWorkerPrepare(0,3);});
 {
  std::unique_lock<std::mutex> lock(gateLock);gate.wait(lock,[]{return migrationWaiting;});
 }
 check(!processor_updown_lock.try_lock()); // CPU exit cannot cross the migration window
 std::thread exiting([]{exitCpu(4);});
 {
  std::lock_guard<std::mutex> lock(gateLock);releaseMigration=true;gate.notify_all();
 }
 drawing.join();exiting.join();
 check(threads[0].blocks==1 && !threads[0].bound_processor && processors[4].state==PROCESSOR_OFF_LINE);
 check(gFBBindings[0].state==kPongFBSkipped);
 std::printf("PASS actual affinity/XNU unbind: %u non-assumed layouts, MIDR variants, disabled/partial/ineligible/prebound/RT/migration failures, 3 unique workers, bounded logs, dequeue/reinsert, CPU-exit serialization and unbound fallback\n",layouts);
}
'''
with tempfile.TemporaryDirectory(prefix='fb-affinity-',dir=os.environ.get('TMPDIR')) as temporary:
    temporary=Path(temporary);test=temporary/'test.cpp';binary=temporary/'test'
    test.write_text(prelude+internal+'\n'+bind+'\n'+unbind+'\n#define printf IOLog\n'+header+'\n#undef printf\n'+checks.replace('pongFBWorkerPrepare','pong_fb_worker_prepare'))
    command=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(test),'-o',str(binary)]
    built=subprocess.run(command,text=True,capture_output=True,timeout=60)
    (a.output_dir/'compile.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise SystemExit(built.stdout+built.stderr)
    result=subprocess.run([binary],text=True,capture_output=True,timeout=30)
    (a.output_dir/'test.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,end='')
    (a.output_dir/'result.json').write_text(json.dumps({'status':'PASS' if result.returncode==0 else 'FAIL','source':str(a.xnu),'sanitizers':['address','undefined'],'device_execution':False,'benchmark':False},indent=2)+'\n')
    if result.returncode:raise SystemExit(result.returncode)
