// SPDX-License-Identifier: MIT
#include <cstdint>
extern "C" {
unsigned pong_test_sync_calls=0;
uint32_t pong_test_sync_phase=0;
int32_t pong_test_sync_result=0;
uint64_t pong_test_sync_coverage=0;
int32_t tu_darwin_sync_host_range(uint64_t,uint64_t,uint32_t) {return (int32_t)0xe00002f0u;}
int32_t tu_darwin_sync_host_batch(const uint64_t *,uint32_t,uint32_t phase,uint64_t *covered) {
 pong_test_sync_calls++;pong_test_sync_phase=phase;*covered=pong_test_sync_coverage;return pong_test_sync_result;
}
}
