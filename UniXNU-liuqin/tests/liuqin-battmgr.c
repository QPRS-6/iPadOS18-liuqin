// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../drivers/PongPower/ShengBattmgr.h"

static uint32_t get32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t *p, uint32_t n) {
    p[0]=n; p[1]=n>>8; p[2]=n>>16; p[3]=n>>24;
}
int main(void) {
    ShengBattmgr b; sheng_battmgr_init(&b);
    sheng_battmgr_refresh(&b,1);
    b.limit_due=900000; // accidental policy input still must not produce USB_SET
    uint8_t request[24], response[24]; unsigned requests=0, temp=0, full=0, design=0, cycles=0;
    while (sheng_battmgr_next(&b,request)) {
        assert(++requests<33);
        uint32_t opcode=get32(request+8), property=get32(request+16), value=0;
        assert(opcode==SHENG_BATTMGR_OP_BATTERY_GET || opcode==SHENG_BATTMGR_OP_USB_GET);
        assert(get32(request)==SHENG_BATTMGR_OWNER);
        if (opcode==SHENG_BATTMGR_OP_BATTERY_GET) {
            assert(property!=12 && property!=15 && property!=20 && property!=21);
            switch(property) {
            case 0:value=1;break;
            case 2:value=1;break;
            case 4:value=9400;break;
            case 7:value=4177000;break;
            case 9:value=1146000;break;
            case 13:value=3070;temp++;break;
            case 16:value=724;cycles++;break;
            case 17:value=8600000;design++;break;
            case 18:value=6927000;full++;break;
            }
        } else {
            switch(property) {
            case 0:value=1;break;
            case 1:value=4831000;break;
            case 3:case 4:case 5:value=500000;break;
            case 7:value=1;break;
            }
        }
        memset(response,0,sizeof(response));
        put32(response,SHENG_BATTMGR_OWNER);put32(response+4,1);
        put32(response+8,opcode);put32(response+12,property);put32(response+16,value);
        sheng_battmgr_receive(&b,response,sizeof(response));
    }
    assert(temp==1 && full==1 && design==1 && cycles==1);
    ShengBatterySnapshot s; sheng_battmgr_snapshot(&b,&s);
    assert(s.charge_known && s.present && s.percent==94);
    assert(s.external && s.charging && !s.fully_charged);
    assert(s.voltage_mv==4177 && s.current_ma==1146);
    assert(s.temperature_valid && s.temperature==3070);
    assert(s.full_mah==6927 && s.design_mah==8600 && s.cycles==724);
    assert(s.raw_mah==0 && s.time_to_empty_min==-1 && s.time_to_full_min==-1);
    assert(b.limit_sets==0 && b.limit_tries==0);
    puts("PASS: liuqin property IDs, centi-C, current sign and read-only requests");
}
