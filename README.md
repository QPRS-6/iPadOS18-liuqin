# iPadOS18-liuqin — source drop

XNU / Darwin 24 port of **iPadOS 18.7.10 (22H374)** to the **Xiaomi Pad 6 Pro
(liuqin, SM8475)**, with the Metal stack that keeps Metal on real hardware
(Adreno 730 through metal2vulkan/Turnip).

This archive contains sources, configuration and patch series only. It does
**not** contain the userdata/root image, the built kernel, any IPSW, the iOS
SDK, firmware blobs, or host toolchains.

## Layout

```
iPadOS18-liuqin/
  UniXNU-liuqin/        device repository: kernel drivers, XNU patch series,
                        userland, build kits, board config (board.toml,
                        configs/liuqin-board.json, dts/liuqin.dts, porting.toml);
                        build, directory and hardware-audit notes under docs/
                        (BUILD.zh.md, LAYOUT.zh.md, BACKLIGHT-AUDIT.zh.md, …)
  UniXNU-sheng/         shared Sheng package sources the device build links
                        (QcomRpmh, AppleARMBacklight reference, probes)
  metal/
    bridge/             PongMetalBridge build recipe and artifact
                        (Cargo.toml points at UniXNU-liuqin/drivers/PongMetal/
                        bridge/lib.rs; the .dylib is the built guest library)
    metal2vulkan/       Metal-to-Vulkan runtime sources
                        (validation corpus and recordings removed)
    mesa/               Mesa 26.2.6 working tree used for Turnip
                        (upstream checkout — NOT committed to this repository;
                        fetch Mesa 26.2.6 and apply mesa-delta to reproduce it)
    mesa-delta/         patch of the Turnip changes against the local clean
                        Mesa 26.2.6 snapshot, for review
```

## Versions

| Part | Version / revision |
|---|---|
| XNU | 11417.140.69 (Darwin 24.6.0) |
| Guest OS | iPadOS 18.7.10, 22H374, iPad7,11 arm64 |
| Mesa | 26.2.6 |
| Board identity | SM8475, 8 CPU, Adreno 730, NT36532 TDDI, KTZ8866 backlight |

## Build entry points

The device repository owns the toolchain and the kernel preparation. A
from-zero walkthrough (host tools, XNU checkouts, SDK/MIG, ADT, Metal stack,
root image) is in [`UniXNU-liuqin/docs/BUILD.zh.md`](UniXNU-liuqin/docs/BUILD.zh.md)
(Chinese), with the directory map in `docs/LAYOUT.zh.md`.

```sh
cd UniXNU-liuqin
python3 ./unixnu build kernel -j 8 -D .
# or the narrower scripts in scripts/ (build-pong-format2.py and friends)
```

The kernel build needs the usual local inputs (Xcode, XNU source checkouts,
iOS SDK, MIG). Paths and required environment variables are documented in
`UniXNU-liuqin/README.md`, `local.toml.example` and `porting.toml`.

The Metal bridge is built from the device repository sources:

```sh
cd metal/bridge
cargo build --release --target aarch64-apple-ios --features ipados18
```

`Cargo.toml` currently carries the author's absolute paths for the frontend
object and the local `metal2vulkan` checkout; adjust them for your tree.

## Device status at the time of this drop (2026-11-17)

Verified on hardware:

- Boots to the SpringBoard desktop over APFS on the GPT partition named `hfs`.
- Touch works (`pong_touch=3` selects the NT36532 probe level;
  `liuqintouchpace=8` paces the bit-banged transfers).
- The shutdown slider powers the tablet off. iOS shutdown ends with launchd
  exiting; the `pong-init-poweroff` XNU patch turns that into
  `PEHaltRestart(kPEHaltCPU)` and the platform hook issues PSCI `SYSTEM_OFF`.
  Ordinary panics still stay on screen (the hook declines once a panic began).
- Metal renders through the Adreno 730 path; GPU stability work is ongoing.

Still in progress:

- KTZ8866 brightness control: the device defconfig now builds it
  (`CONFIG_XNU_PONG_BACKLIGHT=y`). The chip answers on i2c1 and the volume keys
  change the panel level, but the BackBoardServices settings slider is still
  ineffective — see `UniXNU-liuqin/docs/BACKLIGHT-AUDIT.zh.md`.
  `CONFIG_XNU_PONG_POWER` stays off.
- Battery, audio, Wi-Fi and the sensor set remain work in progress; Bluetooth,
  pen and keyboard input are now in-tree but still being brought up. See
  `UniXNU-liuqin/porting.toml`.

## Changes in the 2026-11-17 refresh

194 files in `UniXNU-liuqin/` (140 added, 54 modified, none deleted) over three
commits on the `liuqin` branch, ending at `63e1729` — GPU, input, power and
userland runtime work:

- Turnip gains an in-tree Darwin backend: `drivers/PongAdreno/turnip/tu_darwin_*.cc`
  submits to the Adreno user client directly, paired with the Mesa-side half in
  `patches/mesa/0064-pong-darwin-backend-v2-integration.patch`. The same area
  carries pvtmem transactional growth (0059), suballoc failure ownership (0060),
  the active device lease (0062), allocation-failure evidence (0063) and
  imported GMEM edges plus scoped memory pressure (0066), with QoS idle
  placement (0003) and bounded oslog replay (0004).
- Touch and native input: recovery policy/confirm, fault history, full report,
  autocopy status, startup confirm and SPI-timing patches; the new
  `drivers/PongInputNative` and the native-input entry patch.
  `docs/TOUCH-PREVIEW-PERF.zh.md` traces the firmware-redownload storm behind
  the long input stalls.
- Bluetooth: new `drivers/PongBluetooth` (HID user client, exit-PC and
  user-fault tracing, ZBT snapshots), the `QcaBluetooth` `qbt_transport` source
  pinned in `configs/pong-native-sources.toml`, a userland bootstrap, and the
  legacy-to-extended LE translation with IOHIDFamily prelink.
- Pen and keyboard: static reversal of the Oplus system daemon in
  `docs/OPLUS-PEN-RE.zh.md`, `userland/PongPenEvent.h`, keyboard
  creation/preferences and the input/UI job scripts.
- Power: KTZ8866 backlight is compiled in; `docs/BACKLIGHT-AUDIT.zh.md` records
  the chip's device evidence and the BackBoardServices path that still defeats
  the settings slider. Battery/ADSP-GLINK work is written up in
  `docs/BATTERY-LIUQIN.zh.md`.
- Also in this drop: CPU placement policy in `drivers/ArmCpu`, UFS log policy,
  Metal app diagnostics, and new docs — `INPUTS.zh.md` (what must be fetched
  locally), `NATIVE-INPUT-PHOTO.zh.md`, plus updated BUILD/LAYOUT guides.

The previous refresh (2026-11-10: `pong_fb_flip` double buffering,
`pong_fb_blend`, scanout fallback, 64 MB kernel I/O map) is in git history.

## Licensing

Each subtree keeps its own license files (`LICENSE`, `LICENSE-GPL-*`,
`THIRD_PARTY_NOTICES.md`). Apple restore binaries and vendor firmware are not
included; fetch them locally with the documented scripts.
