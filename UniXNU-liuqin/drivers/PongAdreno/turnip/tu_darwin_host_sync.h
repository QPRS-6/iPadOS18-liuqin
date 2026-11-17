/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Retained host ranges only. An ambiguous device owner yields no coverage,
 * so the frontend performs its existing conservative cache maintenance. */
__attribute__((visibility("default"))) int32_t
tu_darwin_sync_host_range(uint64_t address, uint64_t bytes, uint32_t phase);
__attribute__((visibility("default"))) int32_t
tu_darwin_sync_host_batch(const uint64_t *ranges, uint32_t count, uint32_t phase, uint64_t *covered);
#ifdef __cplusplus
}
#endif
