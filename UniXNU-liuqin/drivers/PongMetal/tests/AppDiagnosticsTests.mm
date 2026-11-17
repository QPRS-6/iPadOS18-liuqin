// SPDX-License-Identifier: MIT
#include "../PongMetalAppDiagnostics.h"
#include <cassert>
#include <sys/resource.h>

static void existing(int) {
 const char text[]="existing handler preserved\n";
 (void)write(STDERR_FILENO,text,sizeof(text)-1);_exit(42);
}
int main(int argc,const char **argv) {@autoreleasepool {
 const rlimit noCore={0,0};setrlimit(RLIMIT_CORE,&noCore);
 assert(argc==2);
 if(!strcmp(argv[1],"gates")) {
  assert(!pongmetaldiag::requested("pong_gpu_appdiag=10"));
  assert(!pongmetaldiag::requested("other_pong_gpu_appdiag=1"));
  assert(pongmetaldiag::requested("a=0 pong_gpu_appdiag=1 b=1"));
  auto before=NSGetUncaughtExceptionHandler();
  pongmetaldiag::install("pong_gpu_appdiag=10");
  assert(NSGetUncaughtExceptionHandler()==before);
  puts("PASS exact diagnostic boot-argument gate");return 0;
 }
 if(!strcmp(argv[1],"existing"))signal(SIGABRT,existing);
 pongmetaldiag::install("pong_gpu_appdiag=1");
 if(!strcmp(argv[1],"exception"))
  @throw [NSException exceptionWithName:@"PongTestException" reason:@"authored diagnostic fixture" userInfo:nil];
 raise(SIGABRT);
 return 99; // A fatal signal must never return here.
}}
