# PongInputNative

Independent input transport for Xiaomi Pad 6 Pro (liuqin, SM8475), targeting
iPadOS 18.7.10 / 22H374. This directory does not call the previous Novatek
driver, GPIO SPI, firmware loader or recovery chain.

## Current scope

- Native GENI SE4, CS0, requested 9.6 MHz, GPI paired TX/RX/event rings.
- SM8475 APPS EE displacement `0x10000` is applied to context, doorbell,
  command, scratch and interrupt offsets relative to the DT resource.
- Separate exact ring SID `0x5b6` and wrapper data SID `0x5a3`, owned native
  4 KiB stage-1 mapping; no new bypass route and no global SMMU reset.
- Refuses allocated APPS GPI contexts, other wrapper DMA configuration,
  masked/conflicting stream routes and unavailable context-bank ownership.
- Wrapper clocks require enable-vote readback. Only AHB branches with their
  documented HWCG bit set accept an idle HALT; core HALT remains checked.
  HWCG is never disabled on the shared wrapper. Votes are not DMA proof.
- Real GIC command acknowledgements and completion events are required.
- Device48 supplies actual transport evidence: both TX/RX START IRQ/state2,
  four SPI transfers/six real completion records, trim `20 00 00 32 65 03`.
  This is not yet working touch, a cure for legacy stalls or pen readiness.
- Bounded failure snapshots precede panic text. DMA-related failures retain
  memory/context ownership, fault only the owned streams and disable this GPI
  instance's IRQ mask. No GPIO fallback or synthetic successful device.
- FWINFO transfer failure is recorded as FAIL and quarantines DMA just like
  a trim transfer failure; successful bytes alone do not validate firmware.

`pong_touch_native=1` runs preparation only. `=2` attempts actual GPI startup,
Novatek trim and FWINFO reads. These are transport trials, **not working touch
or pen implementations yet**. No input service or Apple Pencil capability is
published. Old code is retained for rollback, but the native boot branch must
not invoke it.

`pong_touch_native=3` is the native35 **CPU-mapping-only photo diagnostic**.
It maps the GPI range using XNU but never reads/writes the mapped registers,
changes wrapper votes/pins/SE4, allocates DMA, changes SMMU or starts input.
`PHOTO G01`..`G05` bracket descriptor creation, CPU mapping, descriptor release
and VA query; `PHOTO DONE` returns to normal startup without advertising input.
There is no artificial panic/halt/delay to hold a screen. Mapping success is not
evidence of MMIO accessibility, GPI ownership or DMA communication.

`pong_touch_native=4` is native36's **one-context-read photo diagnostic**.
After real wrapper clock prerequisites and CPU mapping, it reads only APPS
instance1 TX context0 at `0x914000` once. `PHOTO R01` precedes the load;
`PHOTO R02` records its returned value without claiming ownership or DMA.
It does not scan other contexts, write GPI, allocate channels, attach IRQs,
configure SE4/SMMU/DMA or publish input. A single MMIO may still fault/stall;
no software timeout or suppressed panic pretends to make that access safe.

`pong_touch_native=5` is native37's **read-only APPS GPI context scan**.
After wrapper prerequisites and CPU mapping, it reads TX/RX/event context0 and
IRQ-type status on allowed instances1..6 (at most 24 loads), with `PHOTO S01`
before and `S02` after each load. It stops at any allocated/pending state without
clearing it. No reserved instance0, GPI writes, other SE access, IRQ attachment,
SMMU/DMA or input startup. A completed scan observes register state, not a lease
or exclusive ownership, and does not establish SPI/DMA communication.

`pong_touch_native=6` is native38's **existing shared-SE mode inspection**.
It reads only engines already voted on (excluding target SE4) and first checks
each documented GCC branch. A halted branch or lost vote SKIPs the dependent
SE access; it never enables an unknown engine. `PHOTO E00` brackets branch
status, E01 precedes CPU mapping, E02/E03 bracket one DMA-mode read per engine.
No SE command/pin/clock change, GPI writes/scan, IRQ, SMMU/DMA or input startup.
The guarded helper also replaces the earlier vote-only shared-SE preflight.

`pong_touch_native=7` is native39's **target SE4 clock/capability inspection**.
Only an unvoted, halted SE4 may receive its local TCXO/div1 RCG configuration
and vote; a live/voted compatible clock is preserved, other ownership/config
or pending update SKIPs. C01..C06 bracket clock reads/writes and readiness.
T01/T02 precede CPU maps; T03/T04 bracket five read-only FW/status/interface/
DMA-mode/wrapper-version loads. No pin, SE mode/command, IRQ, SMMU/DMA or input
startup. Read completion does not endorse firmware or prove SPI communication.

`pong_touch_native=8` is native40's **read-only APPS SMMU observation**.
It changes no clocks/SE/GPI, maps only the SMMU CPU window, reads IDs/SCR0/GFSR,
then bounded SMRs and only target-related S2CRs for ring SID0x5b6/data SID0x5a3.
Unsupported geometry/client policy/fault or masked/duplicate/invalid target
routes SKIP without reset or takeover. A01 precedes mapping; A02/A03 bracket
each load (maximum261). No bank/ASID allocation, page tables, route writes,
IRQ/DMA or input startup. The view is not a transactional ownership lease.

`pong_touch_native=9` is native41's **read-only bank/slot/ASID plan**.
It freshly observes every SMR/S2CR, rejects any newly active target route,
tracks translated-bank references, reads SCTLR and active TTBR upper ASIDs,
and inspects FSR only on disabled/unreferenced stage1 candidates without clearing.
At most646 loads. It reports two invalid slots and an observed eligible bank/
ASID, not a reservation or permission to write; no clocks, DMA buffers/tables,
register writes, IRQ/DMA/input startup or ownership locks. Later programming
must independently revalidate and acquire real ownership.

`pong_touch_native=10` is native42's **real native4K mapping preparation**.
It freshly checks quiescent GPI/other SE and SPI/GPI target state, allocates
real contiguous DMA/table backing, and selects two still-invalid slots plus
a disabled/unreferenced/fault-free bank/unused ASID under the shared SMMU lock.
Existing target routes or unaccounted active formats refuse replacement.
It preserves QUP's ACTLR (no GPU override), programs the owned native4K context
through the real generic API, requires register/TLB/fault readback and installs
only the two exact routes. The CPU MMIO span is genuinely1MiB, consistent with
ID1 and NativeUfs; no synthetic enlarged adapter range. M01/M02 and W01/W02
bracket allocation/programming/stores; returned stores are not readback proof.
Success retains all mapping ownership; failures fault only owned routes and
retain resources. No pin/SE-mode, GPI IRQ/rings, SPI transfer, firmware or input
startup is attempted in this stage. Global SMMU/other banks/routes remain intact.

Native43 repeats selector10 mapping preparation with **captured expected/actual
readback diagnostics only**. Native42 stopped with StateMismatch7 after15
stores/two TLB polls, without enabling GPI/DMA. The diagnostic prints every
expected/captured CBAR/CBA2R/TCR/TCR2/TTBR/MAIR field and disabled/active SCTLR
expectations, preserving exact validation and cleanup. Captured data predates
cleanup and is not a current active-state assertion. No broad readback mask or
automatic acceptance, pin/SE/GPI/IRQ/SPI/DMA/input changes are added.

Native44 keeps selector10 and explicitly requests `ignore_top_byte=1` after
43.txt isolated TCR2 readback38071 versus expected38011 (TBI0/TBI1 bits0x60).
The generic encoder permits this only for the existing untagged low32-bit
lower IOVA region, no upper region; accepted addresses remain unchanged.
All register comparisons, fault handling, resource retention and downstream
GPI/SPI/DMA/input gates remain exact. No generic driver code or mask is changed.

Native45 keeps selector10 and explicitly requests `shared_asid=1` after44.txt
matched TCR2/all config fields but SCTLR1027 read back27 (ASIDPNE bit1000).
The existing locked allocator selects a globally unused ASID and rejects
unaccounted active formats. Expected SCTLR becomes27; exact comparison,
ownership checks and failure handling remain unchanged. FSR400 is FORMAT,
not a fault bit; older conservative candidate exclusions remain exclusions,
not evidence of a translation fault. No GPI/SPI/DMA/input startup yet.

Native46 selects `pong_touch_native=2` for the **combined native transport trial**
after45 verified mappings: fresh ownership/clock/capability/mapping checks,
SE4 native pin/event preparation, real GIC attachment and GPI context commands,
paired rings, then actual SPI trim/FWINFO transfers. P01/P02, command IRQ/state
snapshots, X01 and at most16 raw completion events identify actual progress.
No synthetic IRQ/completion, GPIO fallback, touch firmware loader or input
publication. Successful transfers and NT36532 trim still require device logs;
failure quarantines only owned resources and returns to normal boot.

Native46 is on hold after the owner's photo showed pin16 PAf010000 then the
panic snapshot hook. The source incorrectly omitted the GPIO tile offset:
liuqin DT f000000+100000, mainline TLMMf100000 and working backlight agree.
Native47 fixes effective TLMM to f100000/span300000, compiles assertions for
pin16=f110000 and pin19=f113000, validates range and reads back only QUP4 mux/
drive bits. This is a definite board-address correction; the photo did not
contain the actual panic text. Same combined selector2 trial, no old GPIO SPI,
root/backlight/battery/GPU changes or synthetic IRQ/transport success.

Firmware initialization, report IRQ ownership, touch delivery and second-gen
Xiaomi pen decoding remain gated on genuine native transport device evidence.
The verified pen configuration chain is documented in
`../../docs/OPLUS-PEN-RE.zh.md`; this transport does not execute the Android
helper or change battery authentication/charging state.

## References and build

Register/descriptor facts: Linux v6.17 `spi-geni-qcom.c`, `gpi.c`,
`gcc-sm8450.c`, and SM8450 device tree, with liuqin reference pinned to
`59413493369901ffc7bae4b2be707e2b0d511673`. Board-specific constants live in
`PongInputNativeBoard.h`; GPI instance/stream slots/bank are never SID-as-index.
Generic XNU allocation, ArmCpu interrupt ownership and ARM 4 KiB table helpers
are platform dependencies, not dependencies on the old touch code.

Build from the device repository with `scripts/build-pong-input-native.py` and
the kernel aggregate's `PONG_NATIVE_INPUT=1` selection. Build success does not
establish hardware DMA, touch or pen correctness. No native DMA/input success
is recorded; the first failed prerequisite device trial is described below.

2026-11-12 host result: native33-r2 narrow compilation and fresh full kernel/shim
build succeeded at device revision `b68654b` plus uncommitted changes, XNU
`43a90889846e00bfb5cf1d255cdc0a701a1e05a4` (11417.140.69). The archive's
undefined imports contain only XNU/platform/4K helpers, no legacy touch calls.
This supersedes the unbooted first native33 candidate, which omitted the APPS
EE displacement. Kernel/shim build receipts are under
`/Volumes/share/build/services33/final` and
`/Volumes/share/logs/pong/services33/final-r2`; image hashes/audits and
benchmarks were skipped by request. Installed queue26 is unchanged.

Device `33.txt` (native33-r2, 2026-11-12) shows native selection and legacy
startup exclusion, then FAIL at wrapper slave-AHB `0x27008=0x80000002` and
dependent-work SKIP. No GPI/SMMU/DMA/trim trial was reached. Linux v6.17
`clk-branch.c` explicitly skips HALT polling when a documented HWCG bit is set;
`gcc-sm8450.c` marks both wrapper AHB clocks with bit1, but not core clocks.
Native34 corrects this over-strict prerequisite and logs each vote/CBCR/HWCG
result. This source correction does not establish DMA operation or identify
the previous driver's touch-stoppage cause.

2026-11-12 native34 host build: fresh kernel and quiet shim succeeded with
the same battery26 inputs; image `shim-boot-liuqin-native-input34-quiet.img`
is 59191296 bytes, command line 948 bytes including NUL. Receipts are under
`/Volumes/share/build/services34/final` and
`/Volumes/share/logs/pong/services34/final`.

Native34 is **on hold after a reported device reboot**. The owner's photograph
shows wrapper prerequisites PASS and GPI ownership RUNNING, but no completed
GPI check or DMA evidence. `34.txt` and its raw log retain only 136 lines/8 KiB
of early startup, before the photographed stage, without a panic. Mapping,
MMIO access, watchdog and other fatal causes are not yet distinguished.
Do not repeat this image or treat a software polling timeout as protection
against a non-returning MMIO access. The owner confirms pstore is unavailable
on this device; use the saved PONGLOG when it contains this stage, plus screen
photos/video. Native35 deliberately stops before register access to isolate
CPU mapping without repeating the failed native34 sequence. No input capability
or native DMA success is claimed.

2026-11-12 native35 host result: narrow compilation and fresh full kernel/shim
build succeeded with battery26 inputs. Mapping-only quiet image
`/Volumes/share/shim-boot-liuqin-native-input35-maponly-quiet.img` is 59191296
bytes; selector=3, boot command 948 bytes including NUL. Receipts are under
`/Volumes/share/build/services35/final` and
`/Volumes/share/logs/pong/services35/final`. The owner reports native35 entered
the desktop, but no PHOTO G05/DONE record was supplied: this shows the reported
boot reached UI, not whether mapping succeeded or the optional probe returned
after failure. No GPI MMIO/DMA was permitted by mode3. No image hashes/audits
or benchmarks run. Capture instructions:
`../../docs/NATIVE-INPUT-PHOTO.zh.md`.

2026-11-12 native36 host result: narrow archive and fresh full kernel/shim
build succeeded, revision `b68654b` plus uncommitted changes, XNU 11417.140.69
at `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`.
`/Volumes/share/shim-boot-liuqin-native-input36-oneread-quiet.img` is 59207680
bytes, selector=4, command line 948 bytes including NUL. Receipts are under
`/Volumes/share/build/services36/final` and
`/Volumes/share/logs/pong/services36/final`; queue26 unchanged,
image verification/benchmarks skipped by request.

Device `36.txt` confirms mode4 and legacy exclusion, completed CPU mapping,
`PHOTO R02` value0 from actual PA `0x914000`, and one-read DONE. The owner reports
desktop entry. This is the first native GPI context-load evidence, not complete
ownership, IRQ, SPI or DMA proof; native34's reset remains unexplained. Native37
extends only the read-only APPS context inspection, with per-load checkpoints.

2026-11-12 native37 host result: narrow archive and fresh full kernel/shim
build succeeded with the same battery26 inputs, revision `b68654b` plus
uncommitted changes, XNU 11417.140.69 at `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`.
`/Volumes/share/shim-boot-liuqin-native-input37-gpiscan-quiet.img` is 59207680
bytes, selector=5, command line 948 bytes including NUL. Receipts under
`/Volumes/share/build/services37/final` and
`/Volumes/share/logs/pong/services37/final`; queue26 unchanged and
image verification/benchmarks skipped by request.

Device `37.txt` confirms all 24 TX/RX/event/IRQ-type loads on APPS1..6 returned0,
followed by scan PASS/DONE; the owner reports system entry. This validates only
the observed GPI reads, not an ownership lease or IRQ/SPI/DMA startup.
Native34's remaining shared-SE inspection is still unverified. Native38 adds
documented GCC branch preconditions and per-access photo records before those
other-SE mode reads, retaining the existing backlight/other engines unchanged.

2026-11-12 native38 host result: narrow archive and fresh full kernel/shim
build succeeded with battery26 inputs, b68654b plus uncommitted changes,
XNU11417.140.69 at `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`.
`/Volumes/share/shim-boot-liuqin-native-input38-sharedse-quiet.img` is 59207680
bytes, selector=6, boot command948 bytes including NUL. Receipts under
`/Volumes/share/build/services38/final` and
`/Volumes/share/logs/pong/services38/final`; queue26 unchanged,
image verification/benchmarks skipped by request.

Device `38.txt` confirms SE1/SE5 branch values0x4220 and mode values0 from
PA0x984258/0x994258, followed by PASS/DONE; the owner reports no reboot.
Neither shared inspection nor GPI reads establish native34's reset cause.
Native39 inspects only guarded SE4 clock setup and controller capabilities;
its GENI FW revision is not Novatek trim/FWINFO or input readiness.

2026-11-12 native39 host result: narrow archive and fresh full kernel/shim build
succeeded with battery26 inputs, b68654b plus uncommitted changes, XNU11417.140.69
at `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`.
`/Volumes/share/shim-boot-liuqin-native-input39-se4caps-quiet.img` is59207680
bytes, selector=7, boot command948 bytes including NUL. Receipts under
`/Volumes/share/build/services39/final` and `/Volumes/share/logs/pong/services39/final`.
Queue26 unchanged, image verification/benchmarks skipped by request.

Device `39.txt` confirms idle/unvoted SE4 clock configuration to0x100001,
vote0x4002cbc0 and ready branch, then FW0x102/STATUS0/IF1/DMA1/WRAP0x30080000
and DONE; the owner reports system entry. This establishes the observed GENI
SPI/GPI capability state, not completed SPI/DMA or Novatek firmware readiness.
Native40 inspects only SMMU geometry and target routes before any mapping write.

2026-11-12 native40 host result: narrow archive and fresh full kernel/shim build
succeeded with battery26 inputs, b68654b plus uncommitted changes, XNU11417.140.69
at `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`.
`/Volumes/share/shim-boot-liuqin-native-input40-smmuro-quiet.img` is59211776
bytes, selector=8, command line948 bytes including NUL. Receipts under
`/Volumes/share/build/services40/final` and `/Volumes/share/logs/pong/services40/final`.
Queue26 unchanged, image verification/benchmarks skipped by request.

Device `40.txt` confirms ID0=4c017e71/ID1=6000004b/ID2=5111,
SCR0=200406/GFSR0, 113 slots/75 banks/128 global pages, EXIDS0, and118 returned
loads with no valid ring/data target match. No bank/ASID inspection, mapping
write or DMA occurred. Native41 observes free slots and bank/ASID candidates
before any programming; candidate status is not an ownership lease.

2026-11-12 native41 host result: narrow archive and fresh full kernel/shim build
succeeded with battery26 inputs, b68654b plus uncommitted changes, XNU11417.140.69
at `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`.
`/Volumes/share/shim-boot-liuqin-native-input41-planro-quiet.img` is59211776
bytes, selector=9, command line948 bytes including NUL. Receipts under
`/Volumes/share/build/services41/final` and `/Volumes/share/logs/pong/services41/final`.
Queue26 unchanged, image verification/benchmarks skipped by request; the later
native41 device observation is described below.

Device41 observes free107/activeM1banks0/eligible68, slots112/111 bank73 ASID74,
not reserved. Referenced1..5/74 and faulted0 FSR400 excluded. Linux/UFS confirm
the CB layout; bypass M0 UFS still owns its referenced bank. Native42 now uses
real1MiB MMIO and locked mapping preparation rather than the old e0000 span
which the generic programming API would reject. No sibling driver is changed.

Native42 narrow archive and fresh full kernel/shim build succeeded on2026-11-12,
b68654b plus uncommitted changes, XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
`/Volumes/share/shim-boot-liuqin-native-input42-map-quiet.img` is59211776 bytes,
selector10, command949 bytes including NUL. Receipts under
`/Volumes/share/build/services42/final` and `/Volumes/share/logs/pong/services42/final`.
Queue26 unchanged; image verification/benchmarks skipped by request.

## Native50: touch and native SPI stylus delivery candidate

`pong_touch_native=12` runs the same guarded fresh firmware initialization,
then enables vendor event-page7b and requires actual host-command ACK before
publishing a separate pen queue. Continuous108-byte real SPI reads match the
vendor109-byte read including index. Touch checksum/id/geometry is validated;
pen format01 decodes big-endian XY/pressure/distance and signed tilt, FF leaves,
F0 is metadata, unknown formats/bounds do not become input. Vendor pen checksum
option is off; no checksum, buttons, battery authentication or Bluetooth
semantics are invented. No Apple Pencil identity is spoofed.

Actual finger changes reach the existing generic BSD `/dev/pong-touch` queue;
the old transport/loader is never invoked. `/dev/pong-pen` is a new independent
48-byte queue, consumed by the matching UI runtime as a standard HID stylus:
tip/range, XY,4095-level pressure and signed tilt. Pen in-range suppresses palm/
finger delivery; stream failure releases contacts/pen and stops, not reloads.
Polling is explicit (8ms sleep); **GPIO21 report IRQ is not attached or faked**.
Real GPI completion IRQ/state validation remains mandatory for every transfer.

The matching runtime updates only `/pong/libpong-ui18.dylib`, retaining touch/
buttons/wake and physical display-off policy. Source/host builds do not prove
native firmware startup, pen2 decoding or successful iPadOS HID delivery.
Queue26 is retained for rollback. The owner now authorizes hfs writes; first
capture the current unmounted filesystem, then change only the UI library and
package a fresh root, preserving current Data/SystemOS/Metal. No stale APFS
baseline may overwrite device changes. No device is currently connected.

Native50 host kernel/shim build passed returncode0 (59490304B, selector12,
command949B including NUL); matching runtime-r2 also compiled/codesigned.
`/Volumes/share/shim-boot-liuqin-native-input50-touch-pen-quiet.img`, runtime
`/Volumes/share/build/services50/runtime-r2/libpong-ui.dylib`. The runtime uses
standard HID report pressure/tilt when the report-device backend exists; if not,
it uses the existing event-system stylus route for actual pressure and logs that
tilt is unavailable there, rather than guessing private fields or claiming success.
No device50 firmware/input/HID results exist. ADB and fastboot are empty: the
current filesystem has not been captured and the matching hfs image has NOT
been produced/flashed. Installation must first use the new guarded live-hfs
capture/update builder; see `/Volumes/share/tmp/services50/INSTALL.zh.md`.
All host builds remain separate from hardware results. No background build runs.

2026-11-13: the owner will capture/flash on the Linux host, not pass USB into
the VM. The builder now accepts a completed Linux REC capture plus receipt via
`--capture`, and only generates simg in that mode (no VM/device writes). A
synthetic owned APFS image test passed as uid501 with `-owners off`; data-only
replacement retains the existing file inode owner/mode and needs no chown/sudo
or global host permission change. The current device source is still required
before actual OS editing. Instructions:
`/Volumes/share/tmp/services50/LINUX-INSTALL.zh.md`. No root/pen success is
inferred from these host tests; native50 hardware results remain unverified.

Final owner choice2026-11-13 supersedes capture-preservation workflow: restore
the local baseline, do not preserve device-added records/databases, owner will
rebuild them. Reused battery26 clone/mount and original
`scripts/build-pong-userdata-simg.py` on a new OS copy, updating only the UI
library. Matching root is now built:
`/Volumes/share/pong-ipados18-userdata-32g.queue27-native-touch-pen.simg`,
11432519984B sparse/logical32GiB. This does NOT preserve current device changes.
Receipt `/Volumes/share/build/services50/root/build-receipt.json`; queue26,
original inputs, SystemOS/Metal/battery/backlight baseline and rollback assets
left intact. No capture required, no VM partition write/boot, no device50 input
verdict. Linux owner flashes hfs with-S512M then temporary boots50.

## Device50 failure and native51 corrections

50.txt confirms all4 native firmware sections, autocopy0, resetA0 and FW19/e6,
but no input device was published: the new gate incorrectly compared postboot
R_DLM to primary DLM golden (it equals last Header CRCcd21b90f), and treated
sensor18000/28800 as12-bit pixels. This was a driver interpretation failure,
not proof of a hardware CRC failure or a userland/HID rejection.

Native51 verifies exact CRC bank destination/length/golden BEFORE download,
then compares every active payload byte in actual SRAM BEFORE BOOT_RDY/running
mutable memory (201924B, no retry). Boot done/no-error CRC flags, reset state,
version/inverse and strict observed board sensor geometry remain required;
R registers are scratch diagnostics, not falsely-labelled primary section CRCs.
No broad validation-mask or synthetic readiness workaround.

Vendor finger records are BE16 XY with pressure at data98+slot in108-byte
reports, not12-bit packed coordinates or pressure at data62. Pen uses the same
sensor units. The corrected parser bounds/normalizes18000x28800 into existing
16-bit BSD/HID output; checksum, id/duplicates, pen format/pressure/tilt guards
remain. Host regression and preboot-verifier mock tests and narrow compile
pass; device51 payload/report/HID proof still pending. Rootqueue27 and runtime-r2
are unchanged; kernel-only trial, no hfs reflash needed.

Native51 fresh full kernel/shim also passed returncode0:
`/Volumes/share/shim-boot-liuqin-native-input51-sensor-quiet.img`59490304B,
selector12/949B args including NUL. b68654b+uncommitted changes, XNU11417.140.69/
43a90889846e00bfb5cf1d255cdc0a701a1e05a4. Host receipts under
`/Volumes/share/build/services51/final` and `/Volumes/share/logs/pong/services51/final`.
No native51 device byte/read/report/HID result or VM boot/write yet; Linux owner
temporary-boots this image with the already installed queue27/runtime-r2.

## Device51 report failure / native52 acquisition

Device51 proves all201924 preboot SRAM bytes, exact golden banks, firmware19/e6/
geometry/boot flags and actual pen command ACK. Both input queues published;
then0 valid/32 rejected **ungated poll** samples stopped the common source before
userland services opened. No rejected raw bytes/reason were retained, so neither
checksum nor coordinate failure is proven. User confirms touch and pen inactive.

Native52 waits for actual normal-run resetA3 (not merely initA0), configures only
owned GPIO21 input/raw rising status latch and keeps IRQ-enable bit0ZERO. It
does NOT enable unowned GIC/PDC routes or claim report IRQ delivery. Already
enabled GPIO21 owner is rejected; exact config readbacks and guarded restore.
Readiness comes from polled leaf latch/observed rise, with owned W1C beforeSPI.
Idle diagnostics (at most8) cannot dispatch or kill the source. Documented77
startup packets are not input/corrupt reports; checksum/id/geometry/pen guards
remain. Only32 rejected READY packets stop/release, no firmware reload/retry.
At most16 raw snapshots preserve all108 bytes and decoder/checksum/GPIO reason;
normal-state timeout also captures one raw frame. Host readiness/parser/verifier
tests and narrow compilation pass; native52 hardware readiness/input still
unverified. Rootqueue27/runtime-r2 unchanged, no hfs update needed.

Native52 fresh full kernel/shim passed returncode0:
`/Volumes/share/shim-boot-liuqin-native-input52-ready-quiet.img`59490304B,
selector12/949B args including NUL; b68654b+uncommitted changes, XNU11417.140.69/
43a90889846e00bfb5cf1d255cdc0a701a1e05a4. Host receipts services52/final and
logs/pong/services52/final; no device52 readiness/frame/HID result or VM boot/
write. Linux owner temporarily boots only this image with existing root27.

## Device52: uncleared GPIO pending; native53 ACK0/direct bursts

`/Users/qprs/Downloads/52.txt:637-811` again proves firmware and now A3 normal
state/pen command ACK. The first16 raw108-byte images are identical: checksum
expected0c/received00, GPIO input0/status1; data96 includes reset stateA3.
Source then stops on0 valid/32 rejected samples. Neither valid new frames nor
32 genuine rising edges are proven by this repeated pending bit.

The new driver's GPIO ACK was wrong: SM8450 PINGROUP leaves `intr_ack_high`
false and Linux `msm_ack_intr_status` writes **0**. Native52 wrote1 using the
unrelated GPI W1C convention, so a raw-enable setup latch was not cleared.
The old host readiness mock erroneously cleared status on any write, hiding
this mismatch. Native53 fixes the ACK0 constant, initial/each-ready clear and
bounded before/after readbacks. It preserves raw-rising polling/IRQ-enable0,
all ownership/firmware/A3/checksum/id/geometry/pen gates and real GPI IRQs.

Report acquisition now matches vendor work_func: one explicit event-page
selection after commands, then direct110-byte full-duplex bursts (index+dummy+
108 data). Generic addressed reads/writes invalidate that report-page state;
no per-frame page write or fabricated completion is introduced. Idle snapshots
stay diagnostic-only and cannot dispatch/fail the ready source. The repeated
buffer's relation to true data-ready transitions still requires device evidence.

Host tests now model actual ACK0 semantics, raw-enable setup latching, and run
the real report loop across60s synthetic idle plus two injected external edges:
only the two ready frames dispatch DOWN/UP; no phantom-ready kill. Narrow
native archive compilation passed. These are host regressions, not actual
native53 readiness/input proof. Next combined kernel-only trial keeps queue27
and UI runtime-r2, with the existing touch and pen delivery paths active.

Native53 fresh full kernel/shim passed returncode0 on2026-11-13:
`/Volumes/share/shim-boot-liuqin-native-input53-ack-quiet.img`59490304B,
selector12/949B command including NUL, b68654b+uncommitted source,
XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4. Host receipts under
`/Volumes/share/build/services53/final` and `/Volumes/share/logs/pong/services53/final`.
No device53 cleared/new-ready/report/HID evidence, no VM device boot/write.
Linux owner onlyboots53; keep installed root27 and existing database state.

## Device53 touch milestone / native54 pen-focused candidate

The owner reports native53 touch usable and stable. `Downloads/53.txt` shows
ACK0 pending1->0, real DOWN/MOVE/UP in kernel and BackBoard, and last printed
report counters2186 valid/0 bad. This is stability in the supplied trial, not
an indefinite stress-test claim. Native53/queue27 are retained as touch fallback.

Pen7b ACK and real hover XY/tilt reach the virtual pressure service with API
dispatch0; the owner still cannot use the pen. Both first12 log budgets ran out
on hover (touch0/pressure0), so later tip pressures are UNKNOWN, not proven0.
Seven rejected pen samples lack reasons. Standard report-device path is absent;
the observed virtual path has no tilt delivery.

Native54 keeps the native53 touch/firmware/ACK0/direct-report behavior and pen
acceptance rules. Kernel adds separate tip/hover/leave/rejection raw evidence,
per-reason counts and raw/accepted pressure peaks; first hover can no longer
consume tip evidence. The userland virtual stylus now carries real sample.touch
in button-mask bit0, matching Apple's standard TipSwitch encoding; previously
it was always0 even on a tip sample. Pressure and touch are never synthesized
from hover. Userland also separates tip/transition budgets and dispatch totals.
This is a confirmed encoding correction, not a demonstrated fix for pen writing.

Matching native54 UI runtime/root update is required for the tip-button change.
Use the owner's chosen original simg packer/local-base rebuild workflow; keep
native53 and queue27 intact. Host tip-HID/parser/actual touch-loop regressions
and narrow compilation pass. No native54 device tip/pressure/HID/app evidence yet.

At the owner's request native54-r2 disables normal finger-event/raw-frame,
GPIO per-frame and periodic touch-count traces in the active input path and
BackBoard bridge. Pen hover/tip/transition/rejection/pressure/dispatch evidence
has independent bounded budgets. Functional touch parsing/delivery stays;
prerequisite/fatal error snapshots remain. Earlier54 artifacts predate this
logging request; use the distinct54-r2 kernel and runtime with matching root.

Native54-r2 full kernel/shim and runtime-r2 compiled successfully; root28 built
using the original sparse packer on a clone of the prepared queue27 OS:
`/Volumes/share/shim-boot-liuqin-native-input54-pen-r2-quiet.img`59490304B,
`/Volumes/share/pong-ipados18-userdata-32g.queue28-native-pen-tip.simg`11432552752B
(logical32GiB). Selector12/949B args, XNU11417.140.69/43a90889 and uncommitted
device work atop b68654b. Local-baseline database rebuilding is the owner's
chosen workflow; original native53/queue27 remain available. Host receipts:
services54/final-r2, services54/root and logs/pong/services54. No device54 pen
tip/drawing result or VM device flash/boot. Linux installation/test instructions:
`/Volumes/share/tmp/services54/INSTALL.zh.md`.

## Device54: pen packets absent / native55 source follow-up

54.txt contains0 hover/0 tip/0 rejection,515 FF/out packets and0 pressure peaks;
no pen samples reached the virtual HID sender. The owner confirms normal finger
touch and that the same pen was removed from the magnetic edge and tested on
the lit screen. This round does not exercise or disprove the UI tip-bit fix.

Native55 restores the GPIO status read AFTER ACK0 that was accidentally removed
with per-frame touch logging. This matches native53's actual MMIO sequence;
it is not proof that the missing read caused pen loss. Finger traces stay off.
At most4 idle/no-pen snapshots read known host/reset/FWINFO and GPIO state,
always restore the report page, and log raw values without claiming that they
prove effective pen enable/detection. No extra chip commands, re-enable loop,
firmware reload, pressure-rule changes, Bluetooth or battery/auth writes.
First2 FF/out pen packets are recorded too. Touch acquisition remains active.

Host report-loop and known-address/page-restoration tests and narrow native
compile pass. Native55 device result is pending. Installed queue28/UI54-r2 stay;
next trial is kernel-only, preserving native53/queue27 as verified-touch fallback.

Native55 full kernel/shim passed returncode0:
`/Volumes/share/shim-boot-liuqin-native-input55-pen-source-quiet.img`59490304B,
selector12/949B args including NUL; b68654b+uncommitted source,
XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4. Receipts under
`/Volumes/share/build/services55/final` and `/Volumes/share/logs/pong/services55/final`.
Onlyboot55 with installed root28/UI54-r2; no VM device write/boot or device55
pen result. Touch traces remain off and actual pen evidence is still required.

## Device55 / native56: real tip data, main-screen routing and measured tilt

55.txt now records533 accepted tip samples/624 hover, pressure peak2987 and
real tipButton1 events accepted by the virtual API (last UI total420 tip,
dispatchErrors0). The owner reports completely no on-screen response. Therefore
packet reception/API acceptance are proven, application routing is not.
183 reports were wrongly discarded by the whole-packet +/-60 orientation gate.
The owner subsequently confirmed Pen2 supports tilt; the earlier no-tilt draft
was only host-built and is superseded before deployment.

Native56 retains actual signed tilt, including61..65. Physical plane angles
within[-90,+90] carry an explicit valid-orientation flag in the existing48B
queue ABI; invalid orientation alone never rejects real XY/pressure/contact.
The userland converts measured plane angles to the polar representation used
by the specialized iOS stylus constructor. No fabricated or estimated tilt.

The runtime uses the existing retained main-screen digitizer for a hand
collection containing an actual type0 stylus child11, with real range/tip/
pressure/coordinates. It no longer depends on an unassociated virtual Pen
service for this path. Screen lifetime and hand-to-pen transitions are mutex
serialized; fingers are released before pen ownership and duplicate queued
finger ups cannot erase a live pen collection. The pen reader never removes
the shared screen service. Pen does not become a synthetic finger or Apple
Pencil identity. Existing screen-off/wake rules and muted finger logs remain.
WebKit iOS SPI confirms the polar constructor's range/invert/Touch-option ABI,
stylus pressure0..500 and polar conventions; raw pressure is converted from
0..4095. Actual transition masks preserve moves instead of resending Touch on
every sample. The generic constructor's extra twist argument was removed to
match the verified5-double/boolean_t ABI. Missing orientation/API falls back
to a pressure-bearing stylus child on the same screen, not a dropped packet.

Host parser/encoding/touch-loop tests, narrow kernel archive and runtime-r2
compile pass. A real host IOKit synthetic polar event confirmed constructor/
Touch/pressure/angle fields; it does not test iPadOS drawing. Native56 needs
its matching root29 runtime; original packer/local baseline workflow, with
native53/queue27 touch fallback kept. Native GENI/GPI/FW/ACK0 path retained.

Native56 final tilt-enabled kernel/shim and runtime-r2 built successfully,
kernel returncode0. `/Volumes/share/shim-boot-liuqin-native-input56-pen-screen-quiet.img`
59490304B, selector12/949B command. Matching
`/Volumes/share/pong-ipados18-userdata-32g.queue29-native-pen-screen.simg`
11432528296B/logical32GiB includes the new runtime. Host receipts under
services56/final/root and logs/pong/services56; XNU11417.140.69/43a90889,
device b68654b plus uncommitted work. The owner subsequently supplied `56.txt`
and confirmed the pen is usable. System Scribble has black rectangles;
Freeform crashes on pen strokes, while finger strokes and pen-operated brush/
file controls work. Pressure response and tilt-axis fidelity are not separately
qualified by that feedback. The native56 kernel and runtime-r2 are the input
baseline for Metal57. No VM-side device write/boot was performed.
Earlier no-tilt runtime was superseded before deployment.

Device42 reached actual owned-context stores and local TLB synchronization,
but returned StateMismatch7, cleanup phase6/writes15/polls2/must-retain0.
The driver retains backing/owned fault routes; no successfully verified mapping,
GPI IRQ/rings, SPI/DMA or input startup. Native43 adds field-by-field expected/
retained actual diagnostic without weakening validation or changing setup.

Native43 narrow archive and fresh full kernel/shim build succeeded on2026-11-12,
b68654b plus uncommitted changes, XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
`/Volumes/share/shim-boot-liuqin-native-input43-mapreadback-quiet.img`59211776
bytes, selector10, command949 bytes including NUL. Receipts under
`/Volumes/share/build/services43/final` and `/Volumes/share/logs/pong/services43/final`.
Queue26 unchanged, image verification/benchmarks skipped by request.

Device43 isolated the only config difference: TCR2 expected38011/actual38071
(TBI0/TBI1=0x60); all other fields matched and captured SCTLR0 matched the
pre-enable expectation. Strict validation rejected/cleaned up; no mapped
transport. Native44 requests this TBI policy explicitly within the unchanged
untagged lower32-bit/no-upper boundary, not by masking readback differences.

Native44 narrow archive and fresh full kernel/shim build succeeded on2026-11-12,
b68654b plus uncommitted changes, XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
`/Volumes/share/shim-boot-liuqin-native-input44-tbi-quiet.img`59211776 bytes,
selector10, command949 bytes including NUL. Receipts under
`/Volumes/share/build/services44/final` and `/Volumes/share/logs/pong/services44/final`.
Queue26 unchanged, image verification/benchmarks skipped by request.

Device44 confirmed TCR2 exact38071 and all other config fields matched, then
captured active SCTLR27 versus expected1027 (ASIDPNE1000), strict rejection and
cleanup. FSR400 is FORMAT, not fault; no mapping/DMA/input success. Native45
explicitly selects shared-ASID policy using the existing globally unused-ASID
locked allocator, without changing comparisons or other setup.

Native45 narrow archive and fresh full kernel/shim build succeeded on2026-11-12,
b68654b plus uncommitted changes, XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
`/Volumes/share/shim-boot-liuqin-native-input45-asid-quiet.img`59211776 bytes,
selector10, command949 bytes including NUL. Receipts under
`/Volumes/share/build/services45/final` and `/Volumes/share/logs/pong/services45/final`.
Queue26 unchanged, image verification/benchmarks skipped by request.

Device45 confirms bank73/ASID74, slots112/111, preserved ACTLR3 and owned
native4K context/two exact SID readback PASS. This verifies preparation only;
no GPI IRQ/rings/SPI/DMA/input ran. Native46 proceeds to the combined genuine
transport trial with fresh checks and actual IRQ/completion requirements.

Native46 narrow archive and fresh full kernel/shim build succeeded on2026-11-12,
b68654b plus uncommitted changes, XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
`/Volumes/share/shim-boot-liuqin-native-input46-transport-quiet.img`59228160
bytes, selector2, command948 bytes including NUL. Receipts under
`/Volumes/share/build/services46/final` and `/Volumes/share/logs/pong/services46/final`.
No native46 IRQ/SPI/DMA device result yet; no firmware loader or input published,
queue26 unchanged, image verification/benchmarks skipped by request.

Native47 narrow archive and fresh full kernel/shim build succeeded on2026-11-12,
b68654b plus uncommitted changes, XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
`/Volumes/share/shim-boot-liuqin-native-input47-tlmm-quiet.img`59228160 bytes,
selector2, command948 bytes including NUL. Receipts under
`/Volumes/share/build/services47/final` and `/Volumes/share/logs/pong/services47/final`.
Device47 confirms pin16..19 correct PA/mux84, exact SMMU mappings, real IRQ277,
event and TX ALLOCATE IRQ/state1. TX START failed with global error type4 at
IRQ3, before RX allocation or SPI submission. Global/general/ERROR_LOG were
not retained there; desktop entry is not transport success. Native46 remains
on hold. Queue26 unchanged; no touch firmware loader/input publication.

Native48 follows pinned Linux `gpi_ch_init` startup (1937-1953): allocate and
configure BOTH TX/RX before starting either, retaining actual command IRQ/state
checks. The first fatal IRQ captures global/general status, ERROR_LOG, context/
scratch and current/last command **before any acknowledgement**, publishes an
immutable snapshot with release/acquire, then clears/masks owned IRQ sources.
Combined fatal IRQs do not count as successful command/event acknowledgements.
The worker prints once and preserves probes12/13; no IRQ logging/allocation or
panic MMIO. Register reads are sequential, not a simultaneous hardware latch.
Same combined trim/FWINFO trial, no reload/retry cure, GPIO fallback, synthetic
completion or firmware/input publication. Host mocked actual-method tests and
narrow archive compilation passed; no native48 device transport evidence yet.

Native48 fresh full kernel/shim build also passed returncode0 on2026-11-12:
`/Volumes/share/shim-boot-liuqin-native-input48-pair-quiet.img`59228160 bytes,
selector2, command948 bytes including NUL; b68654b plus uncommitted changes,
XNU11417.140.69/43a90889846e00bfb5cf1d255cdc0a701a1e05a4. Receipts under
`/Volumes/share/build/services48/final` and `/Volumes/share/logs/pong/services48/final`.
At host-build completion no fastboot device was connected; no host partition
writes. The owner subsequently supplied48.txt: paired TX/RX START now receive
actual ACK2/state2; four transfers (3/8/3/18 bytes) produce six validated
completion records with event IRQ/SMMU checks, then trim `20 00 00 32 65 03`.
FWINFO first bytes `38 10 b6 7b` are transport data, not a firmware-validity
verdict. No firmware loader/report IRQ or touch/pen input exists yet. Evidence:
`/Volumes/share/48.txt:560-594`, device receipt
`/Volumes/share/logs/pong/services48/device-observation.json`.
Queue26/Metal unchanged; image verification/benchmarks skipped. The native47
START failure did not recur in this paired-start trial, but the old recurring
touch stoppages and sustained input operation have not been tested or fixed.

Firmware preparation after48 recovered245760 bytes from the locally saved
generated data array, without using any old driver/loader/recovery routine.
Local input: `/Volumes/share/build/services49/firmware/novatek_nt36532_m81_fw_csot.bin`,
provenance: sibling `.source.json`; this is a **reconstructed local copy**, not
an independently retrieved original for comparison. Minimum NVT end-marker,
version19/inversee6 and primary ILM/DLM bounds checks passed; no complete cascade
parser or device load/CRC verification yet. Firmware remains local, not published.
Services49 is preparation only: no new kernel/shim or firmware write, no report/
input implementation. Next is a fresh bounded parser/loader on the proven native
transport, then real report IRQ and input delivery, never calling old helpers.

## Native49: independent firmware and bounded report trial

`pong_touch_native=11` keeps the proven paired transport, then runs a fresh
one-attempt cascade loader. `--firmware`/`PONG_NATIVE_TOUCH_FIRMWARE` supplies
local data to a generated, non-published include; no old getter/driver/loader
symbol is called. The bounded parser validates the board container, all16
records/reserved entries,4 active sections/201924 bytes, overflow, SRAM ranges
and overlaps before the first chip-reset/write. Unsupported shapes fail closed.

Vendor hw_crc2 startup: reset69 at1fb43e, ILM/DLM destination/length/golden CRC,
autocopy56, real GPI downloads, poll **1fc925** to0, clear reset6 bytes, host
AE00, BOOT_RDY1; require resetA0..AF, actual ILM/DLM CRC match and done/no-error,
matching firmware version/inverse and bounded geometry. No nonexistent BLD CRC
enable/POR register, GPIO/panel reset, retry/reload-recovery cure or fake pass.
Host command is address **event+50=125850**; wire opcode d0 only adds SPI write
bit80, not an address offset. Static vendor code confirms this distinction.

Only after firmware gates pass, observe65-byte report samples for at most90s
alongside normal boot. Validate checksum/id/duplicates/coordinates; log at most
12 changed contact samples and periodic sample counts. Poll samples are not
report IRQ counts, and repeated data is not a fresh contact event. Stop after
32 consecutive invalid samples or any transport failure; no recovery loop.
**No GPIO21 report IRQ, HID service, touch dispatch or pen is published yet.**
This candidate combines loader/start validation with real report data, not a
register-only trial. Host GPI/parser/decoder tests, narrow compilation and fresh
full kernel/shim build pass returncode0 on2026-11-12. Image:
`/Volumes/share/shim-boot-liuqin-native-input49-fw-quiet.img`59490304B, selector11,
command949B including NUL; b68654b plus uncommitted changes, XNU11417.140.69/
43a90889846e00bfb5cf1d255cdc0a701a1e05a4. Receipts:
`/Volumes/share/build/services49/final`, `/Volumes/share/logs/pong/services49/final`.
No fastboot device connected/no host boot or partition writes; no device49
firmware/report verdict yet. Tap/slide immediately during boot (even on log
screen), then capture49.txt after90s; this candidate cannot operate the UI yet.
Queue26 unchanged; image verification/benchmarks skipped by request.
