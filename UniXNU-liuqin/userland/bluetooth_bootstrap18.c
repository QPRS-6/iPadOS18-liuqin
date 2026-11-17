/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* 22H374 BlueTool bootstrap: real kernel status only, no fake HCI or firmware replies. */
#include <dispatch/dispatch.h>
#include <errno.h>
#include <os/log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <xpc/xpc.h>
extern xpc_connection_t pong_bt_listener(const char *,dispatch_queue_t,uint64_t)
    __asm__("_xpc_connection_create_mach_service");
static os_log_t logger;
static unsigned trace_count;
static void bootstrap_trace(const char *stage,const char *operation,int result) {
    if(trace_count>=24)return;trace_count++;
    /* Fixed stage/script/sysctl names only; never XPC payloads or addresses. */
    fprintf(stderr,"Pong Bluetooth82 BlueTool: %s %s result=%d\n",stage,operation,result);
}
static int controller_status(const char *name) {
    int result=EIO;size_t length=sizeof(result);
    if(sysctlbyname(name,&result,&length,NULL,0))result=errno;
    else if(length!=sizeof(result))result=EIO;
    bootstrap_trace("sysctl",name,result);return result;
}
static void fill_reply(xpc_object_t message,xpc_object_t reply) {
    const char *id=xpc_dictionary_get_string(message,"kMsgId");int error=ENOTSUP;int boot=0;
    if(id && !strcmp(id,"RunBuiltinScript")) {
        xpc_object_t args=xpc_dictionary_get_value(message,"kMsgArgs");
        const char *script=args && xpc_get_type(args)==XPC_TYPE_DICTIONARY?xpc_dictionary_get_string(args,"script"):NULL;
        if(script && (!strcmp(script,"boot") || !strcmp(script,"init"))){
            boot=1;error=controller_status("kern.qbt_power_on");if(!error)error=controller_status("kern.qbt_boot");
        }
        else if(script && !strcmp(script,"deepsleep")){
            error=controller_status("kern.qbt_power_off");
            if(!error){error=controller_status("kern.qbt_boot");if(error==ENETDOWN)error=0;else if(!error)error=EBUSY;}
        }
        else error=EINVAL;
        const char *label=script && (!strcmp(script,"boot") || !strcmp(script,"init") || !strcmp(script,"deepsleep"))?script:script?"unsupported":"missing";
        os_log(logger,"22H374 bootstrap script %{public}s result %d",label,error);
        bootstrap_trace("script",label,error);
    }
    xpc_dictionary_set_int64(reply,"result",error?1:0);
    xpc_dictionary_set_int64(reply,"errno",error);
    if(!error && boot) {
        char firmware[64]={0};size_t length=sizeof(firmware)-1;
        if(sysctlbyname("kern.qbt_firmware",firmware,&length,NULL,0)) {
            xpc_dictionary_set_int64(reply,"result",1);xpc_dictionary_set_int64(reply,"errno",errno);
        } else if(!length || length>=sizeof(firmware) || !firmware[0]) {
            xpc_dictionary_set_int64(reply,"result",1);xpc_dictionary_set_int64(reply,"errno",EIO);
        } else {
            // Manufacturer29 is verified by owner80 HCI Read Local Version.
            xpc_dictionary_set_int64(reply,"manufacturer",29);
            xpc_dictionary_set_string(reply,"firmware",firmware);
        }
    }
    if(id && !strcmp(id,"RunCommand"))xpc_dictionary_set_string(reply,"output","Unsupported unqualified vendor command");
    bootstrap_trace("reply",id && !strcmp(id,"RunBuiltinScript")?"RunBuiltinScript":id && !strcmp(id,"RunCommand")?"RunCommand":"unsupported",(int)xpc_dictionary_get_int64(reply,"errno"));
}
#ifndef PONG_BLUETOOTH_BOOTSTRAP_TEST
static void handle(xpc_object_t message) {
    if(xpc_get_type(message)!=XPC_TYPE_DICTIONARY)return;
    xpc_object_t reply=xpc_dictionary_create_reply(message);if(!reply)return;
    fill_reply(message,reply);
    xpc_connection_t remote=xpc_dictionary_get_remote_connection(message);
    if(remote)xpc_connection_send_message(remote,reply);
    xpc_release(reply);
}
int main(void) {
    logger=os_log_create("org.unixnu.pong.bluetooth18","BlueTool");
    dispatch_queue_t queue=dispatch_queue_create("org.unixnu.pong.bluetooth18.bootstrap",DISPATCH_QUEUE_SERIAL);
    xpc_connection_t listener=pong_bt_listener("com.apple.BlueTool",queue,XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if(!listener)return 1;
    bootstrap_trace("listener","com.apple.BlueTool",0);
    xpc_connection_set_event_handler(listener,^(xpc_object_t peer){
        if(xpc_get_type(peer)!=XPC_TYPE_CONNECTION)return;
        xpc_connection_set_target_queue(peer,queue);
        xpc_connection_set_event_handler(peer,^(xpc_object_t event){handle(event);});
        xpc_connection_resume(peer);
    });
    xpc_connection_resume(listener);dispatch_main();
}
#endif
