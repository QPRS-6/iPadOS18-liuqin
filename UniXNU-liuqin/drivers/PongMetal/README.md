# PongMetal

Pong 的 Adreno 730 Metal 前端：面向 22H374 的 `PongAdrenoMetal.bundle`、GOP wire、扫描输出呈现与 pipeline cache 导出。

## 适配硬件

- SM8475 的 Adreno 730；要求真实 CP 与 fence 就绪后才把设备交给用户态
- 显示由 GOP wire 扫描输出呈现，格式转换走 `scanout_convert.ll`

## 仓库关系

设备分支：前端派生自上游 [UniXNU/AdrenoMetal](https://github.com/UniXNU/AdrenoMetal)（原 NativeMetal），原生用户客户端派生自 [UniXNU/Adreno740](https://github.com/UniXNU/Adreno740)；固件、供电、SMMU 与扫描输出仍留在 Pong 自己的实现里。

## 说明

- 22H374 上 `_MTLDevice` 是 600 字节（20G81 是 528），插件构建器要求 bridge 记录的实际长度一致才链接。
- `bridge/` 是 Rust 侧（`NativeMetalObjects.rs`）。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## Current 22H374 integration

services66 also applies the scoped-memory policy to the prepared22H374
runtime. Pooled growth is capped at one sixteenth of the advertised heap,
up to64MiB: a256MiB GPU scope has16MiB growth blocks. Required resources
can exceed that pool cap. OOM releases idle blocks on the same heap and
can fall back to the resource's exact size; host imports participate in
the same bounded reclamation. Live placements retain ownership. Eight
pressure records per allocator distinguish requested resource size from
pool growth and report retained pooled/dedicated/import bytes.

`patches/metal2vulkan/0066-pong-scoped-memory-pressure.patch` is recorded in
the bridge's `local_runtime_patches`, alongside its original preparation
identity. Runtime version1695 invalidates older runtime-behavior records.
The actual allocator with a recording Vulkan table passes21 scoped tests;
the old source fails7 of the8 new cases. These checks open no GPU.

The paired Turnip change supplies the GMEM import-edge handling required by
`InPlaceWrites`: a tightly owned linear host image's unaligned edge uses
the existing exact-texel load/store paths and their CCU-reserving layout.
The128-case actual-function regression covers that decision and the existing
aligned/owned/tiled, conditional, forced and debug paths. Pixel behavior is
still a device qualification.

Owner65's `65-36.txt` confirms the high-address probe, GPU publication and
256MiB scopes. Its16 bounded OOM records reach the diagnostic cap and are
followed by Freeform355 draw/import/texture failures. The owner still sees
black/magenta regions and distorted Settings labels, and estimates the
desktop at barely60fps. Preferences305 has only a binary-archive refusal in
this capture; the cause of its text distortion is not established by that
record. Driver submission windows are not measured panel FPS.

Owner63 reports no perceived improvement and supplies `61.txt`, which
contains the actual63 backend markers. Its32 bounded OOM records include17
with enough total free VA but no large-enough contiguous hole, and15 with
insufficient total VA, all in128MiB windows. Freeform's typed error is
`PipelineCreation(OutOfMemory(Device))`. No DeviceLost or mount-failure line
occurs in this supplied interval. The owner also reports26.2.0-d and26.2.6
behave similarly.64 therefore tests a larger kernel address-space profile
with the same installed root35 rather than inferring a version-only cause.

Turnip63 uses the rewritten per-logical-device backend maintained in
`drivers/PongAdreno/turnip`. The existing Metal62 frontend/bridge ABI is
retained. Host-sync calls keep a connection reference during transport;
ambiguous multi-device calls report no coverage and retain frontend cache
maintenance. The core and Mesa-adapter regressions pass ASan/UBSan, and the
core concurrent lifecycle tests pass TSan. Actual graphics and widget-heavy
desktop stability remain device qualifications.

Owner61 reports no Scribble rectangles in this trial, some remaining desktop
frame drops and Freeform graphics errors. `mid3.txt` confirms three display
workers on the A710s and a median reported display-driver cost of2.001ms;
the remaining stalls include real GPU execution/queue waits. These are log
windows, not a controlled FPS comparison.

Freeform first fails backend admission when all16 slots are taken, then a
later process obtains128MiB and reports texture/import OOM followed by nil
render targets and blit errors.62 retains typed command-buffer failures in
`nmb_command_buffer_error`: the error enum, available driver description,
refused call and failed pipeline's encoder/draw/cause. Successful or pending
queries do not settle an encoder. The native Turnip failure reason is logged
separately with a bounded budget; real failures remain failures.

Turnip62 initializes the logical device's VMA from its current kernel lease,
not the temporary physical-device enumeration connection's address. The
enumeration connection is closed and a later open may choose another slot.
The host regression covers all196 secondary probe/reopen pairs; original
addresses fail the simulated kernel scope check when the slot changes.
The older device log does not distinguish all three probe/open stages, so
it does not by itself prove this defect caused a specific observed failure.
The new `Pong Turnip lease` record names probe and active addresses directly.

`Pong Turnip OOM` records requested size, free VA bytes, largest hole and
active scope. BoAlloc/import/map failures and device loss report their native
stage/status. Per-process scope exhaustion, fragmentation and physical-page
failure can then be distinguished before changing allocation/window policy.
The existing16-slot/128MiB secondary limits still need further capacity work;
no complete Freeform fix is claimed from the host checks.

The owner59 photo/video still shows misplaced dark rectangles during pen
input; desktop animation is reported slow. Its supplied console snapshot
ends in early boot with only two completed Metal commits and no pen tips, so
it cannot qualify the interactive paths or the59 synchronization change.

The follow-up review found a second retained-range bug: internal import
collection used Metal's public `texture.buffer` getter, which correctly
returns nil for IOSurface textures.60 uses the actual retained internal
backing, preserving the public getter's semantics and view ancestry.
The host regression executes real IOSurface/view wrappers: the former
getter lookup produces no sync calls and fails; the internal backing reaches
the dynamic exports with exact coverage/error/phase handling and passes.
One successful nonempty-range observation per phase/process records the
path without adding per-frame logs. No unretained or arbitrary pages are
synced. Actual performance and pixel improvements remain device tests.

The60 logger boot extends the finite console snapshot budget to720, so the
later animation/stroke reproduction is not silently excluded after20min.

Owner58 now reaches Freeform's128MiB device, but pen contact crashes during
compute-pipeline creation. The bridge frames resolve to pending dispatch and
pipeline creation; the top driver frames, using the inferred adjacent Turnip
image base, match the private-memory BO reference at offset0x28. The source
published a larger private-memory capacity before allocating its BO, so OOM
poisoned later calls. services59 builds Turnip with transactional growth:
allocate a replacement first, publish capacity only on success, retain the
previous BO/capacity on failure. The original helper fails an injected-OOM
invariant test; the fixed helper passes ASan/UBSan.

Scribble black rectangles remain unlocalized.59 records the requested texture
format/size/samples/storage on refusal and the pass attachments versus the
pipeline color format on mismatch. The crash image list now includes Turnip.
No attachment validation, real OOM result or fatal signal is suppressed; no
guessed format conversion or app-specific renderer workaround is added.

The quality review also found a suballocator BO left dangling after map
failure, and returned suballocations that retained a released reference.
Those ownership states are cleared with exact errors retained; an extracted
actual-helper ASan/UBSan test covers failure/retry/recycling/repeated free.

The frontend's existing retained-range cache synchronization was inactive:
the deployed Turnip exported neither `tu_darwin_sync_host_range` nor its
batch form, and58's kernel reported zero explicit-sync calls.59 implements
these exports over the existing kernel methods19/20. Only a successful full
coverage reply replaces a user-VA flush. Uncovered/older-ABI paths retain
maintenance, and real errors fail the command. No global coherence flag or
cache-maintenance bypass is enabled. Host tests exercise the actual export
bodies and the actual frontend's `dlsym`/phase2/phase1/coverage/error path.

Owner58's78 slow backboardd records have median pre-submit CPU time10.3ms
and GPU execution18.4ms; this is a biased slow-only sample, not an FPS
benchmark. Reducing redundant cache work is expected to help that CPU cost;
hardware responsiveness and black-rectangle results remain device tests.

The latest owner57 trial found Freeform's backend mount refused with NoSpace
before its nil-function assertion, actual WebKit BGRA surface imports refused
OutOfMemory(Device), and SBRendererService's missing `externFunctionNames`.
services58 adds TYPE5 names from the translator's checked immutable library
table (not an empty placeholder and not visible TYPE4 functions), preserving
full-length names. The surface path now evicts optional retired caches and
retries OOM once outside its import lock. Non-compositor optional import holds
are bounded to16MiB/16 entries; active command/resource owners remain retained.
The Freeform exception hook is installed before backend admission so startup
refusals do not hide its crash. No app/shader-keyed rendering fix is used.

These changes address demonstrated refusal paths; neither the disappearance
of Scribble rectangles nor correct Freeform drawing is claimed before the
matching services58 kernel/plugin are tested on the device.

The iPadOS18 branch builds the plugin and object bridge with
`--guest-build 22H374`. `_MTLDevice` is 600 bytes on this restore, compared
with 528 on 20G81. The plugin builder requires the bridge's recorded guest
and instance extent to match, before linking. The kernel publishes
`PongMetal22H374Enabled` only according to the boot policy, and real CP/fence
readiness remains a separate prerequisite for opening the device.

`PongAdrenoMetal.bundle` resides in the prepared OS's real
`/System/Library/Extensions` directory. The iPadOS18 init does not install a
Data-backed union over that directory. The stock 22H374 renderer selects a
real available Metal device, retaining its CPU fallback when no device is
available. The display format adapter selects linear BGRA output.

`patches/metal2vulkan/0010-pong-ipados18-22H374-policy.patch` adds the
separate IOS18_7_10 runtime policy. It reads 22H374's actual format metadata
and preserves Vulkan capability limits rather than claiming an Apple GPU.

The liuqin/22H374 baseline is native56 with queue29. The owner confirms the
pen is usable, and reports black rectangles in system Scribble and a Freeform
crash on pen strokes (finger strokes and pen-operated controls work).
`56.txt` contains completed Metal work but no Freeform crash stack or GPU
hang/reset/SMMU fault. Its modifier-layout warning alone does not establish
a fatal texture import or either symptom's cause.

The frontend forwards all six late color/depth/stencil store-action and
store-option selectors, with checked runtime enum conversions. Initial
attachment options cross the same C/Rust ABI. Under the existing default
single-sample policy, an `Unknown` action retains an inactive resolve plan;
only an accepted resolving setter enables it. A non-resolving setter drops
the plan, and failed encoding never copies. Known-action direct-target and
source-retaining paths remain available. Copies cover the render area and
valid layers; combined depth/stencil copies use GPU-private aspect staging
to preserve the other aspect. Mip shifts, indices and extents are bounded.
Rejected pass parameters return a callable error encoder, matching runtime
refusals; RenderBox can cache its method entries while the command remains
failed and cannot submit.

IOSurface textures and their views retain the actual plane number. Drawable
presentation requires successful command completion, and locally refused
commands return failed IOSurface ownership. CPU transfer-span overflow is
rejected before a Rust slice is created.

Command, pipeline, completion and surface failures are reported immediately
with process/PID and separate bounded budgets. `pong_gpu_appdiag=1` adds
Freeform-only exception/signal evidence on stderr; the signal handler uses
bounded VM reads and preserves fatal termination and existing handlers.
Include `Freeform` in `applog_name` to capture its OS logs. This uses PONGLOG,
not pstore. Renderer behavior is not selected by application name.

Build with the matching translator source and Pong Turnip library:

```sh
python3 scripts/build-pong-metal-bridge.py --guest-build 22H374 \
  --translator /path/to/metal2vulkan-22H374 \
  --output-dir /path/to/metal18-bridge
python3 scripts/build-pong-metal-plugin.py --guest-build 22H374 \
  --bridge-directory /path/to/metal18-bridge \
  --turnip-library /path/to/pong/libvulkan_freedreno.dylib \
  --output-dir /path/to/metal18-plugin
```

These commands compile artifacts without modifying a phone. Source pins,
reviewed deltas and upstream migration notes are recorded in
[the 2026-10-24 sync](https://github.com/Chengdong-Yu/UniXNU-pong/blob/main/migration/ipados18/upstream-20261024.md).

Prepared 22H374 translators carry `.pong-parity.json`; the bridge builder
uses that preparation without applying legacy `metal_level.rs` patches.
It assembles the authored scanout AIR and accepts `--cargo` and `--target-dir`
so a seeded Cargo cache can stay on local APFS while artifacts go to share.

Host regression command (real Objective-C frontend, recording C ABI and
UBSan; the crash fixture checks capture and termination in child processes):

```sh
python3 scripts/test-pong-metal-frontend.py --output-dir /path/to/check-results
```

services57 passed that suite and the arm64 iOS frontend/bridge/plugin build.
The unchanged runtime dependency emits the same four warnings as the queue29
build; C++ builds use `-Wall -Wextra -Werror`. Device graphics results remain
pending. Exact commands, source snapshot and build receipt are under
`/Volumes/share/build/services57/metal-r2` (the Rust build receipt is under
`metal`). The final root receipt is `services57/root-r3`; the pair is indexed
by `services57/deployment.json`. Device evidence is recorded in `porting.toml`.

## 20G81 development reference

This is a local fork of Sheng's NativeMetal frontend and object bridge over
metal2vulkan and Turnip. The compiled path now includes stock20G81 Metal
discovery, a genuine IOAcceleratorES parent, native BO clients and GPU scanout.
**Actual Pong GPU execution and accelerated presentation remain unconfirmed.**
The most recent phone log stops before CP initialization completes.

## Actual adaptations

- `PongMetalPlugin.mm`:20G81 discovery principal class and real NativeAdreno
  ABI preflight, matching Pong0x07030002/2MiB GMEM. It follows Sheng's
  MetalPluginName/NSBundle/accelerator-port route without its diagnostics,
  private iOS17 offsets or fake capabilities. The platform must publish
  actual completed CP and qualified20G81 runtime policy before this entry
  can create a device. The plugin cannot set those readiness properties.
  `build-pong-metal-plugin.py` links a self-contained development bundle
  beside the Rust bridge and Turnip. The PID1 helper presents the bundle
  through a read-only union at `/System/Library/Extensions`, as Sheng does.
  The original Metal discovery loads its principal class; manual MTLAddDevice
  injection has been removed.
- Read the supplied20G81 cache's Objective-C metadata: `_MTLDevice` has
 528bytes and523 own methods, rather than Sheng21H461's536byte extent.
 `PongMetalAbi.h` requires the actual size and selector return/argument ABI
 before creating a Vulkan runtime or allocating the dynamic subclass.
 No private superclass ivars are written.
- Removed Sheng's iOS17 QuartzCore private context/global-offset diagnostic
 reads. Removed its CPU/memory/SHA benchmark and embedded AIR fixtures.
 Ordinary API calls still report real failures; no diagnostic workload runs.
- `supportsFamily:` uses the runtime's real answer. The original reference
 family report is retained as information, without forced Apple GPU claims.
- Restored bounded universal metallib AIR64 extraction for actual libraries.
 GPU binary members are never substituted for AIR.
- Build the Objective-C++ frontend and Rust product bridge for
 `arm64-apple-ios16.6.1`, then link `PongMetalBridge.dylib` against actual
 frameworks, with no undefined-symbol escape in this bridge link.
- Turnip builds the exact0x07030002/FD725 compiler profile and Darwin client
 backend using the shared fixed-size NativeAdreno wire ABI and4KiB BO
 alignment. It does not change the chip to A740/generic FD730.
 The optional raw builtin variant pack is absent; normal runtime IR3
 compilation remains available. No image hashing or shader pack relabelling.

## v96 actual20G81 runtime policy

The local runtime patch adds `MetalLevel::IOS16_6_1`; the Pong bridge selects
it explicitly. Pixel metadata uses20G81's actual exported
`MTLPixelFormatGetInfo` seven-word ABI, with its null feature-set query.
No macOS26/27 flags/cast classes are relabelled as iOS. Vulkan capabilities
still decide which textures the real device can use. Unknown private
reference counts/flags do not borrow Apple M3/M4 values; the device's own
implemented limits/capabilities remain the runtime's real answers.
Unqualified Apple compressed-heap layouts and lossless compressed blits
stay unsupported. The reproducible source patch is
`patches/metal2vulkan/0008-pong-ios16-20G81-policy.patch`.

## Hardware integration and remaining confirmation

Pong CP/GPU-VM/ZAP/native-client startup is now connected, with actual
timestamp completion required before publication. Phone execution is still
unconfirmed. `PongAdrenoAccelerator` uses the supplied20G81 base metaclass,
constructor and destructor. It initializes only that empty original base;
loading every IOGPUFamily dependency would duplicate the native IOSurface
provider. The native GPU service and accelerator share one BO/VM/submission
backend. The compositor owns its large address window and scanout connection.
`PongAdrenoMetal.publish` and `.clients` enable discovery for backboardd,
SpringBoard, CollectionsPoster and WidgetRenderer_Default. The seed file is the publication
policy; there is no second compiled list rejecting wallpaper clients. BOs and surfaces are imported
through the native ABI, and only completed GPU work retires displayed frames.
Successful compilation or a staged bundle does not establish acceleration.

## Rebuild, without image verification or device operations

From the main project root:

```sh
/opt/local/bin/python3.12 scripts/build-pong-metal-bridge.py \
  --translator /Volumes/PongDev/upstream/gpu-pong/metal2vulkan \
  --output-dir /Volumes/PongDev/build/Pong-iOS16-adreno-development/metal

/opt/local/bin/python3.12 scripts/build-pong-turnip.py \
  --mesa /Volumes/PongDev/upstream/gpu-pong/mesa \
  --abi-directory /Volumes/PongDev/upstream/gpu-pong/NativeAdreno \
  --tools-directory /Volumes/PongDev/build/Pong-iOS16-adreno-development/tools \
  --venv /Volumes/PongDev/build/Pong-iOS16-adreno-development/venv \
  --output-dir /Volumes/PongDev/build/Pong-iOS16-adreno-development/turnip
```

The translator uses NativeMetal's seven bridge patches at its pinned
720ef1ce revision. Mesa uses UniXNU's nine Darwin/compiler/import patches
at85fda3b3. Source pin identifiers identify upstream code; no image hashes
or readback checks are performed. Dependency licenses remain in their
source trees: NativeMetal MIT, metal2vulkan GPL/LGPL, and Mesa per-file
licenses. Product archives, frontend objects and exact build commands stay
beside the local libraries; no phone installation or runtime test is run by
these scripts.

## 许可

`LICENSE` 为 MIT（Copyright (c) 2026 NativeMetal contributors）；`NativeGOPWire.h` 与 `NativeHalfCodes.h` 为 BSD-2-Clause-Patent，逐文件 SPDX 为准。
