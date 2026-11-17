/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* liuqin: real controller-backed UART.SK node, not an Apple class/capability stub. */
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSData.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/codesign.h>
#include "PongBluetoothNative.h"
#include "PongBluetoothNativeProtocol.h"
static IOService *radio;
static uint8_t publishedAddress[6];
static uint32_t publishedBaud;
#ifdef PONG_BLUETOOTH_USERSPACE
extern "C" int pong_bt_runtime_client_allowed(proc_t);
extern "C" int pong_bt_runtime_ready(void);
#endif
extern "C" int pong_bluetooth_native_client_allowed(void) {
    proc_t p=proc_self();if(!p)return 0;
    bool entitled=IOCurrentTaskHasEntitlement("com.apple.bluetooth.control");
    if(!pongbt::nativeClientAllowed(proc_best_name(p),true,entitled)){proc_rele(p);return 0;}
#ifdef PONG_BLUETOOTH_USERSPACE
    bool platform=pong_bt_runtime_client_allowed(p)!=0;
#else
    bool platform=csproc_get_platform_binary(p)!=0;
#endif
    bool allowed=pongbt::nativeClientAllowed(proc_best_name(p),platform,entitled);
    proc_rele(p);return allowed;
}
bool pong_bluetooth_native_publish(const uint8_t address[6],uint32_t baud) {
#ifdef PONG_BLUETOOTH_USERSPACE
    if(!pong_bt_runtime_ready())return false;
#endif
    if(radio)return publishedBaud==baud && !memcmp(publishedAddress,address,6);
    IORegistryEntry *parent=IORegistryEntry::fromPath("/arm-io",gIODTPlane);
    IORegistryEntry *existing=IORegistryEntry::fromPath("/arm-io/bluetooth",gIODTPlane);
    if(!parent || existing){if(parent)parent->release();if(existing)existing->release();return false;}
    IOService *node=new IOService;
    if(!node || !node->init()){if(node)node->release();parent->release();return false;}
    OSData *mac=OSData::withBytes(address,6);
    if(!mac){node->release();parent->release();return false;}
    const char deviceType[]="bluetooth";OSData *type=OSData::withBytes(deviceType,sizeof(deviceType));
    if(!type){mac->release();node->release();parent->release();return false;}
    node->setName("bluetooth");uint32_t encoding=1;
    uint32_t ready=0;
    bool properties=node->setProperty("device_type",type) &&
        node->setProperty("transport-encoding",&encoding,sizeof(encoding)) &&
        node->setProperty("transport-speed",&baud,sizeof(baud)) &&
        node->setProperty("local-mac-address",mac) &&
        node->setProperty("PongBluetoothReady",&ready,sizeof(ready));
    mac->release();type->release();
    IOService *platform=IOService::getPlatform();
    bool attached=properties && platform && node->attach(platform);
    bool tree=attached && node->attachToParent(parent,gIODTPlane);
    if(!tree){if(attached)node->detach(platform);node->release();parent->release();return false;}
    parent->release();memcpy(publishedAddress,address,6);publishedBaud=baud;radio=node;node->registerService();
    IOLog("Pong Bluetooth79: real bluetooth node published with H4 UART20 speed=%u; address not logged; no Apple controller/pairing success claim\n",baud);
    return true;
}
void pong_bluetooth_native_ready(bool ready) {
    uint32_t value=ready?1u:0u;
    if(radio)radio->setProperty("PongBluetoothReady",&value,sizeof(value));
}
