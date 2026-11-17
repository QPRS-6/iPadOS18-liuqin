// SPDX-License-Identifier: BSD-2-Clause-Patent
/* Exact 22H374 BKS API: void Set(float, transaction*), float GetCurrent(void).
 * Verified from this build's exported symbols and s0/x0 register use. */
static uintptr_t pong_bks_set_original;
static uintptr_t pong_bks_get_original;
static uintptr_t pong_bks_implicit_original;
static uintptr_t pong_bks_available_original;
static bool pong_bks_submit(float value)
{
    if (value >= 0 && value <= 1.5f) {
        int level = (int)((value > 1 ? 1 : value)*2047+0.5f);
        bool ok = !sysctlbyname("kern.pong_backlight",NULL,NULL,&level,sizeof(level));
        if (!ok) ok = pong_backlight_iokit_set(level);
        static unsigned reports;
        if (__atomic_fetch_add(&reports,1,__ATOMIC_RELAXED)<12)
            fprintf(stderr,"Pong BKS18: set %.4f level=%d %s\n",(double)value,level,ok?"queued":"FAILED");
        return ok;
    }
    return false;
}
static void pong_bks_set(float value, void *transaction)
{
    if (pong_bks_submit(value)) return;
    uintptr_t original = __atomic_load_n(&pong_bks_set_original, __ATOMIC_ACQUIRE);
    ((void (*)(float,void *))original)(value,transaction);
}
static void pong_bks_implicit(float value)
{
    if (pong_bks_submit(value)) return;
    uintptr_t original = __atomic_load_n(&pong_bks_implicit_original, __ATOMIC_ACQUIRE);
    ((void (*)(float))original)(value);
}
static bool pong_bks_available(void)
{
    int level = -1; size_t size = sizeof(level);
    if ((!sysctlbyname("kern.pong_backlight", &level, &size, NULL, 0) &&
            size == sizeof(level) && level >= 0 && level <= 2047) ||
        pong_backlight_iokit_get(&level)) return true;
    uintptr_t original = __atomic_load_n(&pong_bks_available_original, __ATOMIC_ACQUIRE);
    return ((bool (*)(void))original)();
}
static float pong_bks_get(void)
{
    int level=0; size_t size=sizeof(level);
    if (!sysctlbyname("kern.pong_backlight",&level,&size,NULL,0) &&
        size==sizeof(level) && level>=0 && level<=2047) return level/2047.0f;
    if (pong_backlight_iokit_get(&level)) return level/2047.0f;
    uintptr_t original = __atomic_load_n(&pong_bks_get_original, __ATOMIC_ACQUIRE);
    return ((float (*)(void))original)();
}
static uintptr_t pong_bks_patch(const struct mach_header *header,intptr_t slide,
    uintptr_t offset,const uint32_t expected[4],uintptr_t replacement,uintptr_t *original)
{
    const struct mach_header_64 *h=(const void *)header;
    uintptr_t entry=(uintptr_t)header+offset;
    if (!executable_image_range(h,slide,entry,16) || memcmp((void *)entry,expected,16)) return 0;
    vm_address_t trampoline=0;
    if (vm_allocate(mach_task_self(),&trampoline,vm_page_size,VM_FLAGS_ANYWHERE)) return 0;
    /* Both checked prologues are register/stack operations, not PC-relative. */
    uint32_t back[]={0x58000050,0xd61f0200,(uint32_t)(entry+16),(uint32_t)((entry+16)>>32)};
    memcpy((void *)trampoline,expected,16); memcpy((void *)(trampoline+16),back,16);
    sys_icache_invalidate((void *)trampoline,32);
    if (vm_protect(mach_task_self(),trampoline,vm_page_size,FALSE,VM_PROT_READ|VM_PROT_EXECUTE)) {
        vm_deallocate(mach_task_self(),trampoline,vm_page_size); return 0;
    }
    uint32_t jump[]={0x58000050,0xd61f0200,(uint32_t)replacement,(uint32_t)(replacement>>32)};
    /* The wrapper may execute as soon as the entry changes. Publish its
     * original first, and never reclaim an executable trampoline in flight. */
    __atomic_store_n(original,trampoline,__ATOMIC_RELEASE);
    if (!install_compositor_code(header,slide,offset,expected,jump,16,"BKS brightness")) {
        return 0;
    }
    return trampoline;
}
static void pong_bks_image(const struct mach_header *header,intptr_t slide)
{
    if (header->magic!=MH_MAGIC_64) return;
    static const unsigned char uuid[]={0x3a,0x43,0x8d,0x91,0x42,0xc7,0x3a,0x13,
        0xb8,0xa7,0x8a,0xef,0x17,0xb6,0x97,0xc7};
    const struct mach_header_64 *h=(const void *)header;
    const struct load_command *lc=(const void *)(h+1);
    const char *end=(const char *)lc+h->sizeofcmds; bool matched=false;
    for (uint32_t i=0;i<h->ncmds;i++) {
        if ((const char *)lc+sizeof(*lc)>end || lc->cmdsize<sizeof(*lc) ||
            lc->cmdsize>(size_t)(end-(const char *)lc)) return;
        if (lc->cmd==LC_UUID && lc->cmdsize==sizeof(struct uuid_command))
            matched=!memcmp(((const struct uuid_command *)lc)->uuid,uuid,sizeof(uuid));
        lc=(const void *)((const char *)lc+lc->cmdsize);
    }
    if (!matched) return;
    static const uint32_t sc[]={0x6dbd23e9,0xa9014ff4,0xa9027bfd,0x910083fd};
    static const uint32_t gc[]={0xd10083ff,0xa9017bfd,0x910043fd,0x52a7f008};
    static const uint32_t ic[]={0x6dbe23e9,0xa9017bfd,0x910043fd,0x1e204008};
    static const uint32_t ac[]={0xd10083ff,0xa9017bfd,0x910043fd,0x381ff3bf};
    static bool attempted;
    if (attempted) return;
    attempted = true;
    bool get = pong_bks_patch(header,slide,0x74f4,gc,(uintptr_t)pong_bks_get,&pong_bks_get_original) != 0;
    bool set = get && pong_bks_patch(header,slide,0x3ea40,sc,(uintptr_t)pong_bks_set,&pong_bks_set_original) != 0;
    bool implicit = set && pong_bks_patch(header,slide,0x3ea94,ic,(uintptr_t)pong_bks_implicit,&pong_bks_implicit_original) != 0;
    bool available = set && pong_bks_patch(header,slide,0x3ec34,ac,(uintptr_t)pong_bks_available,&pong_bks_available_original) != 0;
    fprintf(stderr,"Pong BKS18: %s set=%u get=%u implicit=%u available=%u\n",getprogname(),set,get,implicit,available);
}
