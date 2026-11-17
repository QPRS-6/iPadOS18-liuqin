/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define PONG_BLUETOOTH_BOOTSTRAP_TEST 1
#define sysctlbyname pong_bt_test_sysctlbyname
#include "../userland/bluetooth_bootstrap18.c"
#include <assert.h>
#include <stdio.h>
static int scenario,power_calls,state_calls,firmware_calls,off_calls;
int pong_bt_test_sysctlbyname(const char *name,void *value,size_t *length,void *new_value,size_t new_length) {
    assert(!new_value&&!new_length&&value&&length);
    if(!strcmp(name,"kern.qbt_power_on")) {
        power_calls++;
        if(scenario==2){errno=EPERM;return -1;}
        *(int *)value=scenario==3?EIO:scenario==4?EBUSY:0;*length=scenario==5?1:sizeof(int);return 0;
    }
    if(!strcmp(name,"kern.qbt_boot")) {
        state_calls++;*(int *)value=scenario==9?ENETDOWN:scenario==12?EIO:0;*length=sizeof(int);return 0;
    }
    if(!strcmp(name,"kern.qbt_power_off")) {
        off_calls++;if(scenario==13){errno=EPERM;return -1;}
        *(int *)value=scenario==11?EIO:0;*length=sizeof(int);return 0;
    }
    assert(!strcmp(name,"kern.qbt_firmware"));firmware_calls++;
    if(scenario==6){errno=ENOENT;return -1;}
    const char *label=scenario==7?"":"resident-HCI11-LMP11-M1d-S38e6";
    size_t n=strlen(label)+1;assert(*length>=n);memcpy(value,label,n);*length=scenario==8?64:n;return 0;
}
static xpc_object_t request(const char *id,const char *script) {
    xpc_object_t message=xpc_dictionary_create(NULL,NULL,0);
    if(id)xpc_dictionary_set_string(message,"kMsgId",id);
    if(script){xpc_object_t args=xpc_dictionary_create(NULL,NULL,0);xpc_dictionary_set_string(args,"script",script);xpc_dictionary_set_value(message,"kMsgArgs",args);xpc_release(args);}
    return message;
}
static void check(xpc_object_t message,int expected,bool metadata) {
    xpc_object_t reply=xpc_dictionary_create(NULL,NULL,0);fill_reply(message,reply);
    assert(xpc_dictionary_get_int64(reply,"result")==!!expected&&xpc_dictionary_get_int64(reply,"errno")==expected);
    assert((xpc_dictionary_get_string(reply,"firmware")!=NULL)==metadata);
    if(metadata)assert(xpc_dictionary_get_int64(reply,"manufacturer")==29);
    xpc_release(reply);xpc_release(message);
}
int main(void) {
    logger=os_log_create("org.unixnu.pong.bluetooth18.test","recording-sysctl");
    const int errors[]={0,0,EPERM,EIO,EBUSY,EIO,ENOENT,EIO,EIO};
    for(scenario=1;scenario<=8;scenario++){power_calls=state_calls=firmware_calls=off_calls=0;check(request("RunBuiltinScript","boot"),errors[scenario],scenario==1);assert(power_calls==1&&state_calls==(scenario==1||scenario>=6)&&firmware_calls==(scenario==1||scenario>=6)&&!off_calls);}
    scenario=1;check(request("RunBuiltinScript","init"),0,true);
    scenario=9;firmware_calls=0;check(request("RunBuiltinScript","deepsleep"),0,false);assert(!firmware_calls);
    scenario=10;check(request("RunBuiltinScript","deepsleep"),EBUSY,false);
    scenario=11;check(request("RunBuiltinScript","deepsleep"),EIO,false);
    scenario=12;check(request("RunBuiltinScript","boot"),EIO,false);
    scenario=13;check(request("RunBuiltinScript","deepsleep"),EPERM,false);
    power_calls=state_calls=firmware_calls=off_calls=0;
    check(request("RunCommand",NULL),ENOTSUP,false);check(request(NULL,NULL),ENOTSUP,false);
    check(request("RunBuiltinScript",NULL),EINVAL,false);check(request("RunBuiltinScript","unsupported"),EINVAL,false);
    xpc_object_t message=request("RunBuiltinScript",NULL);xpc_dictionary_set_string(message,"kMsgArgs","not a dictionary");check(message,EINVAL,false);
    message=request(NULL,NULL);xpc_dictionary_set_int64(message,"kMsgId",1);check(message,ENOTSUP,false);
    assert(!power_calls&&!state_calls&&!firmware_calls&&!off_calls);
    puts("PASS81 BlueTool: 20 real-XPC dictionary/error cases with recording sysctl; denied/failed/pending/short/empty firmware, failed boot and an unconfirmed power-off do not become success. No host Mach service or controller opened.");
    return 0;
}
