/* SPDX-License-Identifier: MIT */
#pragma once
#include "tu_darwin_core.h"
#include "tu_device.h"
#include "tu_knl.h"
#include "tu_queue.h"

VkResult tu_darwin_error(tu_device *, int32_t, const char *, VkResult);
VkResult tu_darwin_bo_init(tu_device *, vk_object_base *, tu_bo **, uint64_t, uint64_t,
                           VkMemoryPropertyFlags, tu_bo_alloc_flags, tu_sparse_vma *, const char *);
VkResult tu_darwin_bo_init_host(tu_device *, tu_bo **, void *, uint64_t);
VkResult tu_darwin_bo_init_dmabuf(tu_device *, tu_bo **, uint64_t, tu_bo_alloc_flags, int);
int tu_darwin_bo_export_dmabuf(tu_device *, tu_bo *);
VkResult tu_darwin_bo_map(tu_device *, tu_bo *, void *);
void tu_darwin_bo_finish(tu_device *, tu_bo *);
void tu_darwin_bo_allow_dump(tu_device *, tu_bo *);
void tu_darwin_bo_set_metadata(tu_device *, tu_bo *, void *, uint32_t);
int tu_darwin_bo_get_metadata(tu_device *, tu_bo *, void *, uint32_t);
void *tu_darwin_submit_create(tu_device *);
void tu_darwin_submit_finish(tu_device *, void *);
void tu_darwin_submit_add_entries(tu_device *, void *, tu_cs_entry *, unsigned);
void tu_darwin_submit_add_bind(tu_device *, void *, tu_sparse_vma *, uint64_t, tu_bo *, uint64_t, uint64_t);
VkResult tu_darwin_queue_submit(tu_queue *, void *, struct vk_sync_wait *, uint32_t,
                                 struct vk_sync_signal *, uint32_t, tu_u_trace_submission_data *);
VkResult tu_darwin_queue_wait_fence(tu_queue *, uint32_t, uint64_t);

struct vk_darwin_sync { vk_sync vk; tu_darwin_sync state; };
extern const vk_sync_type vk_darwin_sync_type;
VkResult tu_darwin_wait_syncs(tu_device *, const struct vk_sync_wait *, uint32_t, bool, bool, uint64_t);
VkResult tu_darwin_publish_signals(tu_device *, const struct vk_sync_signal *, uint32_t, uint32_t);
