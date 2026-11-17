/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_BLUETOOTH_NATIVE_H
#define PONG_BLUETOOTH_NATIVE_H
#include <stdint.h>
#include "qbt_transport.h"
extern "C" int pong_bluetooth_native_client_allowed(void);
extern "C" int pong_bluetooth_native_status(void);
extern "C" void pong_bluetooth_native_counters(uint64_t counters[4]);
bool pong_bluetooth_native_publish(const uint8_t address[6],uint32_t baud);
void pong_bluetooth_native_ready(bool ready);
#endif
