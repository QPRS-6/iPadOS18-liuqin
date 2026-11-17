# PongAdreno

Adreno 730 平台支持：GMU、CP、HFI、ZAP 认证、SMMU 与 scanout，冷启动按原厂顺序进行。

## 适配硬件

- SM8475 的 Adreno 730（Cape GPUCC）：两路 LUCID_OLE PLL 配置，GPU SMMU 为 QSMMU V500（ACTLR `0x32b`）
- GPU 中断 SPI300 / GIC INTID332；SID 0/1 与 mask `0x400` 的 user 与 lpac 上下文
- `pong_adreno_start=1` 才发起一次异步冷启动；否则不碰 GPU MMIO

## 仓库关系

Pong 专属实现，不来自 `UniXNU/Adreno740`（那是 740 的固件与寄存器）。`ArmSmmu4K/` 供 Wi-Fi 与音频共用，`submit/` 是 NativeAdreno 的 Mesa/Turnip 侧。

## 说明

- `PongAdrenoCpProfile.h` 由 NothingOSS SM8475 gen7 源码生成，是 GPL-2.0-only；其余多为 BSD-2-Clause-Patent 与 MIT，逐文件 SPDX 标注。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## Sheng activation chain / Pong20G81

## liuqin / 22H374 GPU client scopes

### services66：导入图像的GMEM边界

`0066-pong-imported-gmem-edges.patch` 为当前Mesa26.2.6补上
`InPlaceWrites` 所需的线性host-import边界处理。显式布局/host指针导入的纹理，
尺寸不是GMEM块的整数倍时，使用已有的精确texel加载、存储路径；
`tu_choose_gmem_layout` 同时保留这些存储所需的CCU区域。像素格式、交换和采样
继续使用这些路径原有的计算。无需更改kernel ABI、地址窗口或时钟。

`tests/turnip-imported-gmem.py` 执行实际load/store/layout函数：128组输入，
旧源在24组紧凑导入输入的三个决策点均错误，新源全部通过ASan/UBSan；
条件/强制加载和调试路径也覆盖。Mesa完整iOS交叉构建通过，GPU像素仍需实机观察。

### services64：可选36位地址空间

机主64-36日志已经记录高地址CP写入正确的 `0x73640036` 和完成的fence2，
但64的探测调用将4字节传给要求整4KiB页的 `acquireDeviceWrites`，因此返回
`0xe00002c2` 并阻断GPU发布。services65改为同步完整目标页，增加该步骤的
状态记录；真实提交/fence/缓存失败及结果不匹配仍保持失败。回归同时运行
实际探测主体和实际内存接口，重现旧误判并验证修正及失败路径。

65-36实机已确认整页同步成功、探测PASS、GPU服务和256MiB租约生效；
图形异常及该窗口内OOM仍然存在，记录于 `logs/pong/services65/device-observation.json`。

`gpuva=32`（默认）使用原4GiB GPU地址范围、16连接和128MiB次要窗口。
`gpuva=36` 选择原生4KiB页表的16GiB范围，32个连接槽，次要窗口256MiB。
backboardd1280MiB、SpringBoard512MiB仍使用原低地址范围。窗口是地址范围，
物理页按实际资源分配。

新窗口从 `0x110000000` 开始；`0x100000000` 的8KiB保留映射用于启动验证。
GPU必须通过真实CP高地址IB读取、MEM_WRITE、fence和CPU结果检查，才发布服务。
失败或前提缺失记录FAIL/SKIP并保留资源。该检查只验证CP高地址基本访问，
完整着色器/显示表现需要真机测试。32位对照保留原窗口布局。

`PongGpuVaPolicy.h` 给出有界页表容量：低36/16GiB共8209页页表，约32MiB。
TBI策略仅允许未标记的低32/低36地址，映射入口仍严格限制范围；上区间、其他宽度
和寄存器不一致继续拒绝。GMU自己的32位配置与SID、固件和电源流程沿用既有实现。
`tests/gpu-wide-va.py` 验证实际页表/上下文编码、高地址翻译、窗口与CP包字段，
直接MMIO绑定仍只在AArch64内核构建。设备结果另行记录。

Turnip的Darwin后端由本仓库 `turnip/` 维护，按连接、设备状态、同步和Mesa适配
重写为独立模块，构建器将源码放入Mesa。每个逻辑设备拥有自己的连接、VA/fence和
回收记录；临时探测、其他设备关闭和host-sync导出并发不会互相接管状态。
锁契约、失败处理和测试入口见 [turnip/README.md](turnip/README.md)。

ABI1 uses one 32-bit GPU aperture. The established backboardd1280MiB and
SpringBoard512MiB windows are retained; fourteen secondary connection slots
each receive a disjoint128MiB window. These are virtual-address scopes, not
wired memory reservations. The final free slot has its own range even when
all earlier slots are occupied. The prior variable poster-first leases could
consume the entire aperture before Freeform opened (`57.txt`: slot15 mount
NoSpace, followed by a nil-compute-function assertion).

Successful global retirement and resource teardown remain prerequisites for
reusing a slot. Failed close logs its process/slot/status and retains live
maps; no forced unmap, live relocation or GPU reset is added. Larger poster
workloads may still exhaust their128MiB scope: that is not hidden by claiming
more GPU address space. The frontend separately bounds optional cache holds
and retries refused imports once after safe cache eviction.

`tests/native-adreno-windows.cpp` checks all sixteen scopes, bounds,
non-overlap and unchanged UI reservations. This host test proves the address
policy, not app rendering. Later device observations are recorded below.

Owner61 `mid3.txt` reaches the16-connection cap24times, including Freeform's
first launch. A later Freeform process opens slot7 but reports private texture
and IOSurface import OOM in its128MiB scope. No failed-close record is present;
this is not evidence of a leaked or safely reclaimable live connection.

The Turnip consumer now uses the lease returned by the actual device open
when constructing its VMA (`patches/mesa/0062-pong-active-device-lease.patch`).
The temporary enumeration connection does not own its range after close.
`0063-pong-allocation-failure-evidence.patch` records allocation stage/native
status, free/largest VA extent and raw device-loss causes. These changes do
not establish that every graphics failure was a stale lease; current scope
capacity and the shared32-bit context remain separate limitations.

Pong uses Nothing's SM8475 bootstrap ordering: CP initialization packets,
real RPTR drain plus RBBM idle, authenticated ZAP, an unfenced nonsecure
switch, then a real CP memory write and timestamp fence. Sheng's ordinary
fence-before-ZAP startup must not replace this board sequence. IRQ/timer,
native user clients and system Metal discovery follow actual CP execution. `NativeAdrenoPwrupList` packs Pong's
register list; `NativeAdrenoIrq` owns and acknowledges Pong SPI300/INTID332.
The former ACK-clear wait at GBIF release has been removed: ACK indicates
halt completion and is not Sheng's release prerequisite.

Pong's Cape GPUCC also needs its two LUCID_OLE PLL configurations before
GMU rate requests: PLL0 L/alpha `0x1d/0xb000`, PLL1 `0x34/0x1555`, with
the Cape calibration/configuration values. Running PLLs are preserved;
stopped PLLs are left in STANDBY for firmware rate control. Initial GMU/HUB
clocks still use GPLL0. CX GBIF halt is released before CM3 boot as in
Nothing's gen7 driver. The GPU SMMU is QSMMU V500, so its ACTLR remains
Pong's DT value `0x32b`, rather than borrowing Sheng's MMU-500 workaround.
Fence timeouts now also latch real SMMU faults and retain DMA memory.

Pong's live DT also declares `gfx3d_lpac` at SID1/mask0x400, in addition
to `gfx3d_user` at SID0/mask0x400. The A730 firmware starts its BR/BV/LPAC
engines before processing PM4; BR at PC0xa2 waits for their THREAD_SYNC
semaphore. Lease exact SIDs0/0x400/1/0x401 in the same single GPU context
so all engines can read the uploaded firmware and shared BOs. The secure
SID2/0x402 remains outside this lease. Include the vendor's terminal dynamic
restore triplet `PIPE_NONE, RBBM_PERFCTR_CNTL, 1` even with counters disabled.

A 256MiB BO pool is requested before fragmentation (halving on allocation
failure). The CPU uses WC buffers and the submit path reasserts real pmap
attributes, matching Sheng. GPU fence waits sleep on IRQ wakeups with bounded
timeouts. The render server retains its own 1GiB GPU address window.

`--metal-accelerator` includes the original20G81 base implementation in the
prelink. Only its empty IOAcceleratorES metaclass is initialized; the original
Apple IOSurface/AGX providers do not replace Pong's working services. The
Pong accelerator class is initialized afterwards and routes native clients
to the same real CP backend. Board clock, GMEM, SID, ZAP and firmware values
remain those of SM8475 rather than the A740 board.

## v99: actual v98 failure and scanout handoff

The v98 pmsg confirms GMU native4K context setup succeeded, then GPU/SQE
preparation returned `0xe00002c2`. SQE is75920 payload bytes, while
`NativeAdrenoMemory::publishForDevice` owns full4096-byte pages. Publish the
owned0x13000 backing, not the short payload; likewise publish the full restore
list page. Buffer budgets remain allocation budgets, not alignments.

`PongAdrenoScanout.cpp` adapts NativeMobileDisplay's offer/wait/complete
transport to Pong's existing Mini framebuffer. A separate backboardd connection
(`0x4148`) exposes only the actual framebuffer and offered IOSurface pages.
It leaves the BO/submission connection available while the worker waits.
`pong_gpu_scanout=1` enables the path. The20G81 Metal plugin imports the real
pages and uses NativeMetal's GPU blit presenter; only real command completion
retires an offered frame. Timeout/error after delivery retains DMA backing and
the previous screen. Before delivery, CPU scanout remains available. Reopening
a rejected scanout connection does not disconnect an existing owner.

Initial supported layout: one full-panel, unscaled, plain BGRA surface with
the panel pitch. Multi-layer/scaled/other-format swaps retain existing CPU
composition. No synthetic workload, readback validation, fake completion,
arbitrary physical-memory API or A740 board profile is introduced.
This source/compiled path alone does not establish hardware acceleration;
actual CP/Metal/scanout results must come from the next device log.

## Sources and differences

- Pong's captured `evidence/recovery-20261020/live.dts`: GPU chip id
  `0x07030002`, model Adreno730v3, Cape GPUCC, GMU `0x03d68000/0x37000`,
  RSCC `0x03d50000/0x10000`, GPU SMMU `0x03da0000/0x40000`, GMU SID 5.
- [Nothing SM8475 source](https://github.com/NothingOSS/android_kernel_msm-5.10_nothing_sm8475/tree/sm8475/t/drivers/gpu/msm):
  gen7_reg.h, adreno_gen7_gmu.c, adreno_gen7_hfi.c, adreno_hfi.h and
  adreno-gpulist.h. The matching gen7-4-0 profile uses 2 MiB GMEM,
  gmu_gen70000.bin, a730_sqe.fw and a730_zap. Its clocks are described in
  drivers/clk/qcom/gpucc-waipio.c, with the `qcom,cape-gpucc` fixup.
- [UniXNU AdrenoFirmware](https://github.com/UniXNU/AdrenoFirmware):
  original firmware structure planner and the basis of the isolated HFI fork.
- UniXNU-Sheng/package/ShengGmu.c: startup structure and one-shot failure
  policy. No A740 register map, firmware hash or clock/voltage votes were copied.
- Earlier port redwood_adreno.c: separation of platform power,
  native4K GMU/GPU mappings, ZAP authentication, CP start and user-client
  publication. Its A642L register/firmware values are not applicable here.

In particular, this Pong vendor HFI implementation reserves 17 header slots:
the table is 210 dwords, followed by queue storage at byte 848 (16-byte
alignment). Three queues are active; the debug priority is 40. The current
Sheng transport instead uses table field 168, page-spaced queues and other
header fields. The Pong fork uses the actual vendor layout and leaves its
reserved fields zero. A matching sequence alone is insufficient: require an
actual ACK packet type. RSCC sequencer memory starts at dword 0x180 here.

Mesa's freedreno device table names this exact `0x07030002` revision
Adreno730v3 / FD725 (A7XX Gen1) with a revision-specific compiler property.
Do not change the ID to FD730/`0x07030001` or A740 to pass a profile check.
The GMU timer's offset relative to Pong's mapping is 0x16220/0x16224,
not Sheng's 0x14220/0x14224. The supplied DT declares 21 context banks and
24 stream slots; these are capacity facts, not permission to take a bank.

## Implemented module

- `PongAdrenoPlatform.cpp`: actual Pong GCC, APSS DRV2 active TCS3 and
  command-db adapters, cold-master ownership, contiguous native4K allocations,
  real cache maintenance, exact GMU SID5/0x405 context setup and bounded
  firmware/HFI/power startup. It checks actual GX-off/CM3-reset prerequisites
  and refuses to take over a live GPU. Until a shared RPMh aggregator exists,
  parent CX/MX votes retain the highest ACTUAL allowed AUX corner without
  changing CPU/GPU frequency. Only TCS3 is used; sleep/wake votes are intact.
  GMU faults are polled during every hardware wait with IRQs explicitly
  masked, fault reporting enabled and retained DMA backing on failure.
  Stage/results are written to the independent ramoops diagnostic pmsg area.
  Normal boot continues after PASS/FAIL/SKIP. GMU completion alone does not
  qualify CP execution or Metal readiness.
  v95 uses real command ISSUED|COMPL plus idle/fresh-transaction observation
  for polled RSC completion, not an enabled-IRQ prerequisite. It checks
  active TCS4 for conflicting ARC work and reclaims only its own trigger.
- `PongAdrenoGmu.c`: SM8475 resource windows/identity, native4K backing
  requirements, firmware load plan, Pong RSCC/PDC sequence, real cache/TCM
  upload, GMU chip-id conversion to 0x07030200, fresh CM3/HFI readiness waits.
  All I/O uses bounded window callbacks and actual platform prerequisite
  callbacks. Timeout and iteration bounds apply; failure after mutation
  poisons the device, attempts reset and retains DMA backing.
- `PongAdrenoHfi.c`: isolated actual-Pong queue serialization and matching
  ACK transport; no fabricated ACK/fence and no GPU-ready service publication.
- `AdrenoFirmware.c`: original structural firmware planner. No runtime
  image hash, image readback audit or benchmark is introduced.
- `PongAdrenoSmmu.c` / `ArmSmmu4K/`: leased native4K GPU/GMU context and
  exact stream attachment, physical table checks, observation and fault-first
  stop. GPU SID0/0x400/1/0x401 and GMU SID5/0x405 are separate exact routes. Capacity
  does not establish ownership. The v94 adapter leases only cold GMU routes,
  applies Pong's actual ACTLR policy and uses explicit synchronous fault
  polling. The CP now has a separate owned bank0 lower-region context in
  `PongAdrenoCp.cpp`; user/LPAC routes are leased while GX is stopped;
  secure and GMU routes are excluded.
  The local fork no longer silently accepts TBI/ASIDPNE readback differences
  observed only on Sheng. A Pong exception requires actual Pong evidence.
- `PongAdrenoPower.c`: reads actual retained command-db aux data and builds
  Pong's 58-word PERF and159-word BW payloads. It uses rounded primary ARC
  corners to select secondary dependencies, actual gfx/mx/cx levels, zero
  DDR average, peak votes and the actual ACV/CN0 encoding. This serializer
  does not invent command-db addresses; the platform adapter issues the
  actual parent RPMh votes before calling it.
- `PongAdrenoClocks.c`: bounded CX GDSC and Cape200MHz GMU/150MHz HUB/XO
  clock setup, GCC and GPUCC branches. Parent clocks/votes and exclusive
  stopped-master ownership are prerequisites. No A740 PLL program is reused.
- `PongAdrenoGmuStartPower`: real matching-ACK PERF -> BW -> CORE_FW_START
  -> selected blocking GX/BW vote after actual HFI readiness. Darwin24 defaults
  to `pong_gpu_pwrlevel=0` (Pong DT 900 MHz, DDR index11); `=11` restores
  the previous 285 MHz request. Frequency, ARC dependencies and bandwidth
  remain paired; GMU acknowledgement is not a measurement of actual frequency.
  Vendor START10
  belongs later in GPU boot before OOB_GPU, not in this cold HFI sequence.
- `PongAdrenoZap.c`: structural split ELF32 MDT planner and actual PAS13
  shutdown/init/mem-setup/auth-reset flow through a bounded SCM adapter.
  Pong's load header selects `a730_zap.b01`; its `.b02` is certificate/hash
  metadata. Repack the actual header and separate signature file as the
  vendor loader does; no assumption about the packed MDT tail is needed.
  Reservation is0x8b91a000/0x2000, with the PAS image span derived
  from the ELF and rounded to4KiB. Metadata must use retained contiguous
  non-cacheable native4K backing (the vendor XPU requirement). No SCM error
  is turned into an assumed previously-loaded success. No host hash is run.
- `submit/`: generic A7xx PM4/ring/CP-init/timer fork from NativeAdreno.
  Pong CP_ME_INIT disables unnegotiated DMS with bit11; GMU timestamps use
  actual SM8475 offsets0x16220/24. These modules compile and link but do not
  run until GX, actual a730 SQE/ZAP, GPU context ownership and a completed
  init fence are established. No A740 hardware-init table is imported.
- `scripts/build-pong-adreno-platform.py`: derives register facts from the
  matching Nothing gen7_reg.h and builds an arm64 kernel archive. The
  generated register header and build report live beside that archive. Use
  kernel mode, general registers only (no SIMD), and the kernel's iOS16.0
  deployment floor; the runtime target remains the existing 16.6.1 system.

All CPU pages/backing translations must be 4 KiB. A 16 KiB hardware TCM
extent or four-page HFI allocation is not a 16 KiB CPU subpage configuration.
The caller must provide real powered/exclusive mappings, real native4K SMMU
ownership and real cache publication before calling the GMU loader. Merely
copying a firmware file or setting a software flag does not establish them.

## Build artifacts and phone state

Local development artifacts are under
`/Volumes/PongDev/build/Pong-iOS16-adreno-development/`. `platform/` contains
the current kernel archive and its exact compiler commands; `metal/` and
`turnip/` contain the iOS16.6.1 userspace bridge and Vulkan library. See
`../PongMetal/README.md` for the remaining private iOS ABI work.

v93 used the saved v92 kernel with an inactive GMU/HFI foundation. v94
attempted startup but recorded `FAIL parent votes`; no CX clock, SMMU or
firmware stage ran. Its later panic was PID1 exiting because a packager
dropped the RAM helper boot argument and ran the old console-stdout helper.
v95 fixes that argument inheritance and the polled completion logic. See
`/Volumes/PongDev/build/Pong-iOS16-v95-adreno-panic-fix/NOTES.md` for the
actual evidence, construction and boot command/result. The software UI,
touch and saved v92/v93 kernels are retained. The userspace Metal/Turnip
libraries are not installed by v94.

## v96 CP and native user client

`PongAdrenoCp.cpp` connects actual gen7-4-0 HWCG/GBIF/protection/restore
facts generated from Nothing's matching source. It uses2MiB GMEM and
uche_gmem_alignment0, not Sheng's3MiB/16MiB window. SQE skips its vendor
header dword. DMS/IFPC/preemption remain disabled; CP init keeps DMS bit11.
GPU SID0/0x400/1/0x401, ACTLR0x32b and bank0 are leased while masters are stopped.
GMU OOB_GPU and START10 require fresh real ACKs; GX power/clock status is
checked before any GX MMIO. The ring then submits CP init, waits for its
real timestamp, authenticates Pong's b01 code/b02 metadata through PAS13,
and submits CP_SET_SECURE_MODE0 with another real fence. No synthetic
workload or content/hash verification runs.

ZAP0x8b91a000/0x2000 is retained before VM allocations when inside the
XNU window; overlap with an existing boot payload refuses authorization.
SCM uses bounded SMCCC64/QCOM A6 resume and real errors; no unconditional
shutdown of an unloaded PAS. HFI bidirectional pages use matching normal
NC/WC CPU attributes, rather than cached volatile reads of stale ACKs.

`PongAdrenoClient.cpp` serves the fixed NativeAdreno1..16 methods on
`NativeAdrenoGpuPublished`: native4KiB BO allocation/import, scoped GPU VA
windows, matching CPU-map descriptors/lease teardown, queues, submission,
real fences/status/counter. Every map/unmap drains actual jobs first;
failed retirement retains ownership. Import pins only the caller's pages.
No arbitrary physical-memory API, AGX ABI, sandbox bypass or mailbox is
included. The first Pong client uses explicit cache maintenance; no
Sheng WC/coherence qualification or per-IB content probes are inherited.

## Remaining hardware qualification

The connected v96 path compiles/links; only actual phone results establish
which stages executed. General asynchronous IRQ/scheduling, reset/resume,
Metal discovery and GPU IOSurface presentation remain unqualified. Current
work uses bounded synchronous completion/fault polling with IRQs masked.
The userspace20G81 bundle is staged privately, with optional Apple heap and
compression assumptions disabled; it is not an automatically exposed Metal
accelerator. No GMU-only success is reported as hardware acceleration.

## 许可

逐文件 SPDX：`PongAdrenoCpProfile.h` 为 GPL-2.0-only（`LICENSE-GPL-2.0-only`）；`AdrenoFirmware.*`、`PongAdrenoClocks.*`、`PongAdrenoGmu.*`、`PongAdrenoHfi.*`、`PongAdrenoPlatform.cpp`、`PongAdrenoPower.*`、`PongAdrenoScanout.*`、`PongAdrenoSmmu.*`、`PongAdrenoZap.*`、`PongScanoutWire.h` 为 BSD-2-Clause-Patent（`LICENSE`）；其余为 MIT，派生自 AdrenoMetal / NativeMetal（Copyright (c) 2026 NativeMetal contributors）。
