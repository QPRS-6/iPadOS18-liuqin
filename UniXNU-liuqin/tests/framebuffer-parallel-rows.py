#!/usr/bin/env python3
"""Compare actual display drawRows serial/parallel with synthetic pixels; no benchmark/device."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
source=a.source.read_text()
def function(signature):
    start=source.index(signature);brace=source.index('{',start);depth=0
    for end in range(brace,len(source)):
        depth+=(source[end]=='{')-(source[end]=='}')
        if not depth:return source[start:end+1]
    raise RuntimeError('Unclosed function')

pixel=function('static inline void\npongBlendPixel')
row=function('static void\npongBlendRow')
blend=function('static inline uint32_t\nblend(')
sample=function('static inline int64_t\nsample_at(')
draw=function('void\nIOMobileFramebuffer::drawRows(')
job=source[source.index('struct RowJob {'):source.index('\n};',source.index('struct RowJob {'))+3]
start=source.index('\t\t/* share index + 1 of fThreads')
helper=source[source.index('\t\tint32_t first',start):source.index('\t\tblend = fb->fHelperBlend',start)]
first='visibleY0 + (visibleY1 - visibleY0) / fThreads'
if first not in source:raise RuntimeError('Presenter partition changed; review fixture')
unscaled=re.search(r'job\.unscaled = (.*);',source).group(1)
unscaled=re.sub(r'\b(px0|px1|py0|py1|sw|sh)\b',r'job.\1',unscaled).replace('fPanelWidth','width')
fixture=r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>
#include <algorithm>
#define check(v) do {if(!(v)){std::fprintf(stderr,"FAIL: %s\n",#v);std::_Exit(1);}} while(0)
static uint32_t gPongBlend=1;
static bool fbcon_overlay_active=false;
static uint32_t fbcon_overlay_top=0;
'''
cls=r'''
class IOMobileFramebuffer {
public:
 uint8_t *fBase=nullptr;
 uint32_t fPanelWidth=64,fPanelHeight=53,fRowBytes=69*4;
 unsigned fThreads=1;
 bool fDiff=false;
 uint64_t fDiffPixels=0;
 uint32_t fDiffLeft=~0u,fDiffTop=~0u,fDiffRight=0,fDiffBottom=0;
 uint32_t fColumn[64]={};
 uint16_t fColumnWeight[64]={};
 void drawRows(const RowJob *,int32_t,int32_t,uint32_t *);
};
struct Rows {int32_t first,last;};
'''
partition='static Rows helperRows(IOMobileFramebuffer *fb,const RowJob *job,unsigned index) {int32_t rows,y0,y1;\n'+helper+'return {y0,y1};}\n'
checks=r'''
int main() {
 unsigned cases=0;uint32_t seed=37;
  for(bool scaled:{false,true})for(bool filter:{false,true})for(unsigned mode=0;mode<3;mode++)for(bool overlay:{false,true})for(unsigned blendMode:{0u,1u})for(unsigned geometry=0;geometry<3;geometry++) {
  constexpr unsigned width=64,height=53,stride=69,guard=32;
  std::vector<uint32_t> input(46*42),initial(guard+stride*height+guard);
  for(auto &value:input){seed=seed*1664525u+1013904223u;value=mode==0?(seed|0xff000000u):mode==1?(seed&0xffffffu):seed;}
  for(auto &value:initial){seed=seed*1664525u+1013904223u;value=seed;}
  RowJob job{};job.pixels=(uint8_t *)input.data();job.stride=46*4;job.sx=2;job.sy=1;
  job.sw=scaled?24:17;job.sh=scaled?17:13;job.px0=scaled?-3:5;job.py0=scaled?-5:3;
  job.px1=scaled?50:22;job.py1=scaled?48:16;job.cx0=std::max(0,job.px0);job.cx1=std::min<int32_t>(width,job.px1);
  if(geometry==1){job.sh=1;job.py0=52;job.py1=53;}
  if(geometry==2){job.px1+=50-job.px0;job.px0=50;job.py1+=47-job.py0;job.py0=47;}
  job.cx0=std::max(0,job.px0);job.cx1=std::min<int32_t>(width,job.px1);
  job.filter=filter;job.unscaled=UNSCALED_CONDITION;
  gPongBlend=blendMode;fbcon_overlay_active=overlay;fbcon_overlay_top=31;
  IOMobileFramebuffer reference;
  for(int x=job.cx0;x<job.cx1;x++) {
   if(filter){int64_t at=sample_at(x-job.px0,job.sw,job.px1-job.px0);
    reference.fColumn[x]=uint32_t(at>>8);reference.fColumnWeight[x]=reference.fColumn[x]+1<unsigned(job.sw)?at&255:0;
   }else reference.fColumn[x]=job.sx+int64_t(x-job.px0)*job.sw/(job.px1-job.px0);
  }
  int32_t visibleY0=std::max(0,job.py0),visibleY1=std::min<int32_t>(height,job.py1);
  auto expected=initial;reference.fBase=(uint8_t *)(expected.data()+guard);std::vector<uint32_t> scratch(46);
  auto inputBefore=input;
  reference.drawRows(&job,visibleY0,visibleY1,scratch.data());
  for(unsigned i=0;i<guard;i++){check(expected[i]==initial[i]);check(expected[guard+stride*height+i]==initial[guard+stride*height+i]);}
  for(unsigned y=0;y<height;y++)for(unsigned x=0;x<stride;x++) {
   bool visible=int32_t(y)>=visibleY0 && int32_t(y)<visibleY1 && int32_t(x)>=job.cx0 && int32_t(x)<job.cx1 && (!overlay || y<fbcon_overlay_top);
   if(!visible)check(expected[guard+y*stride+x]==initial[guard+y*stride+x]);
  }
  if(mode==1 && blendMode)check(expected==initial);
  for(unsigned count:{1u,2u,3u,8u}) {
   auto output=initial;IOMobileFramebuffer parallel=reference;parallel.fBase=(uint8_t *)(output.data()+guard);parallel.fThreads=count;
   std::vector<std::thread> workers;std::vector<std::vector<uint32_t>> local(count,std::vector<uint32_t>(46));
   std::vector<unsigned> coverage(height);
   std::vector<Rows> ranges;unsigned fThreads=count;
   ranges.push_back({visibleY0,static_cast<int32_t>(PRESENTER_END)});
   for(unsigned i=0;i+1<count;i++)ranges.push_back(helperRows(&parallel,&job,i));
   for(unsigned i=0;i<count;i++) {
    Rows range=ranges[i];for(int32_t y=range.first;y<range.last;y++)coverage[y]++;
    workers.emplace_back([&,i,range]{parallel.drawRows(&job,range.first,range.last,local[i].data());});
   }
   for(auto &worker:workers)worker.join();
   for(unsigned y=0;y<height;y++)check(coverage[y]==unsigned(y>=unsigned(visibleY0) && y<unsigned(visibleY1)));
   check(output==expected);check(input==inputBefore);cases++;
  }
 }
 std::printf("PASS %u actual drawRows cases: 1/2/3/8 workers, exact row coverage, opaque/transparent/mixed alpha, blend/copy/scale/filter/clip/overlay, one-row tails, unchanged guards/padding/input\n",cases);
}
'''.replace('PRESENTER_END',first).replace('UNSCALED_CONDITION',unscaled)
with tempfile.TemporaryDirectory(prefix='fb-rows-',dir=os.environ.get('TMPDIR')) as temporary:
    temporary=Path(temporary);test=temporary/'test.cpp';binary=temporary/'test'
    test.write_text(fixture+pixel+'\n'+row+'\n'+blend+'\n'+sample+'\n'+job+'\n'+cls+'\n'+draw+'\n'+partition+checks)
    command=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(test),'-o',str(binary)]
    built=subprocess.run(command,text=True,capture_output=True,timeout=60)
    (a.output_dir/'compile.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise SystemExit(built.stdout+built.stderr)
    result=subprocess.run([binary],text=True,capture_output=True,timeout=30)
    (a.output_dir/'test.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,end='')
    if result.returncode:raise SystemExit(result.returncode)
