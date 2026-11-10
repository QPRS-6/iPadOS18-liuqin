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
                        build and directory guides in docs/BUILD.zh.md and
                        docs/LAYOUT.zh.md
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

## Device status at the time of this drop (2026-11-10)

Verified on hardware:

- Boots to the SpringBoard desktop over APFS on the GPT partition named `hfs`.
- Touch works (`pong_touch=3` selects the NT36532 probe level;
  `liuqintouchpace=8` paces the bit-banged transfers).
- The shutdown slider powers the tablet off. iOS shutdown ends with launchd
  exiting; the `pong-init-poweroff` XNU patch turns that into
  `PEHaltRestart(kPEHaltCPU)` and the platform hook issues PSCI `SYSTEM_OFF`.
  Ordinary panics still stay on screen (the hook declines once a panic began).
- Metal renders through the Adreno 730 path; GPU stability work is ongoing.

Not enabled yet:

- KTZ8866 brightness control. `drivers/PongPower/PongBacklightKtz.cpp` and the
  `pong-i2c-backlight` ADT property exist, but every kernel built with that
  object linked panicked on boot, so the shipped kernel keeps
  `CONFIG_XNU_PONG_POWER=n` / `CONFIG_XNU_PONG_BACKLIGHT=n`.
- Battery, sensors, stylus, audio, Bluetooth and Wi-Fi remain work in progress;
  see `UniXNU-liuqin/porting.toml`.

## Changes in the 2026-11-10 refresh

Nineteen files in `UniXNU-liuqin/` (5 added, 14 modified) on top of the
previous drop — display path, build docs and the touch driver:

- `pong_fb_flip`: double-buffered scanout. Each frame is written into the
  buffer the display pipe is not fetching, the pipe's source address is
  repointed and the control path flushed; after three flips that are not taken
  it falls back to the boot loader's buffer. Ported from HakiOS/mayfly
  (`fb_flip`) — `patches/xnu-ipados18/…-pong-fb-flip.patch`.
- `pong_fb_blend` (same patch, default 1): kernel composition blends by real
  alpha instead of forcing every pixel opaque, which removes the colour bars
  where a composited sheet carries uninitialised RGB behind transparent pixels.
- Scanout fallback: `SWAP_GPU_MS` raised 500 → 1000 ms, and when no GPU frame
  completed the CPU composition path is permitted again instead of returning
  `kIOReturnNotReady` (`pong_scanout_fallback`) — `…-pong-scanout-fallback.patch`.
- `pong-io-map-64m`: the kernel I/O map window grows from 8 MB to 64 MB
  (`osfmk/arm/io_map.c`).
- GPU presenters reworked: `GpuScanoutPresenter.h` copies pitched BGRA rows in
  one dispatch and supplies opaque alpha upstream; scanout and submit paths
  (`PongAdrenoScanout.cpp`, `NativeAdreno{Publish,Submit}.cpp`), UFS transport
  and the NT36532 touch patch updated as well.
- New `docs/BUILD.zh.md` (from-zero build) and `docs/LAYOUT.zh.md` (directory
  map); the device README gained a full acknowledgement list of the upstream
  components this port derives from.

## Licensing

Each subtree keeps its own license files (`LICENSE`, `LICENSE-GPL-*`,
`THIRD_PARTY_NOTICES.md`). Apple restore binaries and vendor firmware are not
included; fetch them locally with the documented scripts.
