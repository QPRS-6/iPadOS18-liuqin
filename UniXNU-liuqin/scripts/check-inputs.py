#!/usr/bin/env python3
"""Verify every external input a Pong build needs, and say how to obtain it.

The repository carries only our own code and patches. Apple's sources, the
IPSW extract, the SDK, firmware, the OS images and the shared build trees are
local inputs and are deliberately *not* committed (see .gitignore). A fresh
clone therefore builds nothing until those inputs exist; this script is the
single place that lists them.

Usage:
    python3 scripts/check-inputs.py            # human table, exit 1 on missing
    python3 scripts/check-inputs.py --json     # machine readable
    python3 scripts/check-inputs.py --quiet    # only problems

Exit code: 0 when every required input exists, 1 otherwise.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

DEVICE = Path(__file__).resolve().parents[1]
HOME = Path.home()
LIUQIN = Path(os.environ.get("LIUQIN", HOME / "liuqin-unixnu"))
WORKSPACE = Path(
    os.environ.get(
        "PONG_WORKSPACE",
        HOME / "Documents/deepseek-harness/default-workspace/iPadOS-liuqin",
    )
)
SHARE = Path(os.environ.get("PONG_SHARE", "/Volumes/share"))

# (group, path, kind, purpose, how to obtain, required)
#   kind: file | dir
#   required=False entries are reported but do not fail the check.
INPUTS = [
    # ---- host toolchain -------------------------------------------------
    ("toolchain", LIUQIN / "bin/python3", "file",
     "pinned host python used by every recipe",
     "install the repository's python into $LIUQIN/bin (or symlink your python3)", True),
    ("toolchain", LIUQIN / "venv/bin/python", "file",
     "venv used by userdata/OS image scripts",
     "python3 -m venv $LIUQIN/venv && $LIUQIN/venv/bin/pip install -r requirements.txt", True),
    ("toolchain", Path("/Applications/Xcode.app"), "dir",
     "clang, codesign and the iPhoneOS SDK driver",
     "install Xcode 16 from the App Store, then xcode-select --install", True),
    ("toolchain", Path("/Users/qprs/liuqin-build3/ld.lld"), "file",
     "LLD used by tools/mkpayload.py and the shim build",
     "set LLD=... to your own ld.lld (any LLVM 18+ build) or symlink it here", True),
    ("toolchain", HOME / "liuqin-metal", "dir",
     "Mesa/Turnip build tree (only needed when rebuilding the GPU stack)",
     "clone the metal tree; a kernel-only build does not need it", False),

    # ---- Apple inputs (never committed) ---------------------------------
    ("apple", LIUQIN / "ipsw-22H374/kernelcache.22H374.macho", "file",
     "original 22H374 kernelcache: kext closure, symbols, HID stack",
     "download the iPad7,11 22H374 IPSW (ipsw.me) and decompress its kernelcache", True),
    ("apple", LIUQIN / "ipsw-22H374/pong-trustcache.bin", "file",
     "trust cache for the extracted components",
     "scripts/prepare-ipados18-* build it from the IPSW extract", True),
    ("apple", LIUQIN / "ipsw-22H374/BuildManifest.plist", "file",
     "22H374 identity and component list",
     "comes with the IPSW extract", True),
    ("apple", LIUQIN / "ios-sdks/iPhoneOS18.6.sdk", "dir",
     "headers, libraries and tbd stubs for arm64 iOS 18",
     "copy from Xcode or the iPhoneOS SDK package; the recipes pin 18.6", True),
    ("apple", LIUQIN / "tools/mig-138/bin/mig", "file",
     "MIG used for the kernel MIG-generated stubs",
     "extract mig-138 from Xcode or Apple's open source cctools", True),
    ("apple", LIUQIN / "tools/mig-138/libexec/migcom", "file",
     "migcom companion of the above", "comes with mig-138", True),
    ("apple", LIUQIN / "xnu-10063/bsd/kern/kern_exit.c", "file",
     "XNU source the kernel is built from (Darwin 24 / 22H374)",
     "download xnu-10063 (Apple open source) and extract to $LIUQIN/xnu-10063", True),
    ("apple", LIUQIN / "xnu-8796/bsd/kern/kern_exit.c", "file",
     "second XNU tree used for trustcache helpers",
     "download xnu-8796 (Apple open source) and extract to $LIUQIN/xnu-8796", True),

    # ---- shared build trees --------------------------------------------
    ("shared", LIUQIN / "UniXNU", "dir",
     "UniXNU shared build tree (./unixnu is run from here)",
     "git clone the UniXNU repository to $LIUQIN/UniXNU", True),
    ("shared", LIUQIN / "UniXNU-sheng", "dir",
     "Sheng SM8550 reference tree the SM8475 seed derives from",
     "git clone UniXNU-sheng to $LIUQIN/UniXNU-sheng", True),
    ("shared", LIUQIN / "userland-22H374/pong-ipados18-system.cdr", "file",
     "OS image that carries the Pong userland dylibs",
     "scripts/build-ipados18.py prepare (see docs/BUILD.zh.md §4)", True),
    ("shared", LIUQIN / "userland-22H374/pong-ipados18-systemos.cdr", "file",
     "SystemOS cryptex image",
     "same prepare step as the OS image", True),
    ("shared", LIUQIN / "userland-22H374/pong-ipados18-data.cdr", "file",
     "pristine Data seed",
     "same prepare step as the OS image", True),
    ("shared", LIUQIN / "ufs-22H374/shngroot-ipados18.bin", "file",
     "userdata image header (shngroot)",
     "scripts/prepare-ipados18-storage.py", True),
    ("shared", LIUQIN / "ufs-22H374/ufs-layout.json", "file",
     "userdata window layout (OS/SystemOS/Data offsets)",
     "scripts/bind-ipados18-storage.py", True),
    ("shared", LIUQIN / "firmware/adreno730", "dir",
     "Adreno 730 firmware blobs (a730_sqe.fw, zap, gmu)",
     "extract from your own phone firmware; never commit", True),
    ("shared", WORKSPACE / "tools/mkpayload.py", "file",
     "Darwin-24 payload packer (kernel -> payload.bin)",
     "the iPadOS-liuqin workspace must be present at $PONG_WORKSPACE", True),
    ("shared", WORKSPACE / "shim/build.sh", "file",
     "boot shim builder", "same workspace", True),

    # ---- third-party driver sources -------------------------------------
    ("driver-src", DEVICE / "refer/ipados18/QcaBluetooth/tools/inspect-ios.py", "file",
     "Mach-O/dyld-cache reader used to patch the Bluetooth daemon",
     "unpack the 22H374 Bluetooth components into refer/ipados18/QcaBluetooth", True),
    ("driver-src", DEVICE / "refer/ipados18/Ath12kWifi", "dir",
     "QCA6490/ath11k source used by the Wi-Fi port",
     "vendor the ath11k/ath12k source (checksummed in configs/pong-native-sources.toml)", True),
    ("driver-src", DEVICE / "refer/ipados18/linux-7.0.14", "dir",
     "Linux 7.0.14 tree that the ath11k build compiles against",
     "download linux-7.0.14 and extract to refer/ipados18/linux-7.0.14", True),
    ("driver-src", DEVICE / "refer/ipados18/xnu-11417.140.69", "dir",
     "Apple XNU export headers referenced by the driver kits",
     "extract xnu-11417.140.69 into refer/ipados18/ (symlink to feeds/ is fine)", False),

    # ---- build-time inputs under the share ------------------------------
    ("share", SHARE / ".", "dir",
     "artifact/log share mounted by the host",
     "mount the host share at /Volumes/share (SMB/9p); the recipes write candidates here", True),
    ("share", SHARE / "build/services67/wifi67.config", "file",
     "Wi-Fi config used by the kernel build",
     "produced by the Wi-Fi bring-up steps (docs/BUILD.zh.md §6)", True),
    ("share", SHARE / "build/services67/firmware/qca6490", "dir",
     "QCA6490 Wi-Fi firmware (amss20.bin, m3.bin, bd_*.elf, regdb)",
     "extract from your own phone firmware; never commit", True),
    ("share", SHARE / "build/services49/firmware/novatek_nt36532_m81_fw_csot.bin", "file",
     "Novatek NT36532 touch firmware",
     "extract from your own phone; checksum in configs/pong-native-sources.toml", True),
    ("share", SHARE / "build/services97/userland/bluetoothd", "file",
     "the patched 22H374 bluetoothd embedded in the kernel runtime",
     "rebuild with the runtime steps (docs/BUILD.zh.md §7) or keep services97", True),
    ("share", SHARE / "build/services97/userland/BlueTool", "file",
     "owner-built BlueTool stand-in", "same runtime step", True),
    ("share", SHARE / "build/services80/kernel/va36/boot-args.txt", "file",
     "baseline kernel command line that every later build derives from",
     "produced by the first qualified kernel build (services80)", True),

    # ---- optional fallbacks ---------------------------------------------
    ("fallback", SHARE / "shim-boot-liuqin-bluetooth80-hci-gpu65-va36-native56-quiet.img", "file",
     "last known stable boot image", "keep it; reflash it to recover from a bad candidate", False),
]


def check(entry):
    group, path, kind, purpose, how, required = entry
    ok = path.is_file() if kind == "file" else path.is_dir()
    return {
        "group": group,
        "path": str(path),
        "kind": kind,
        "purpose": purpose,
        "how": how,
        "required": required,
        "ok": ok,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--quiet", action="store_true", help="only print missing required inputs")
    a = ap.parse_args()

    rows = [check(e) for e in INPUTS]
    missing = [r for r in rows if r["required"] and not r["ok"]]
    optional_missing = [r for r in rows if not r["required"] and not r["ok"]]

    if a.json:
        print(json.dumps({"ok": not missing, "missing": missing,
                          "optional_missing": optional_missing,
                          "checked": len(rows)}, indent=2))
        return 1 if missing else 0

    if not a.quiet:
        group = None
        for r in rows:
            if r["group"] != group:
                group = r["group"]
                print(f"\n[{group}]")
            mark = "PASS" if r["ok"] else ("MISS" if r["required"] else "opt ")
            print(f"  {mark} {r['path']}")
            if not r["ok"]:
                print(f"       purpose: {r['purpose']}")
                print(f"       obtain : {r['how']}")

    if missing:
        print(f"\n{len(missing)} required input(s) missing. See docs/INPUTS.zh.md.")
        for r in missing:
            print(f"  - {r['path']}\n      {r['how']}")
        return 1
    print(f"\nAll {len(rows)} inputs present"
          + (f" ({len(optional_missing)} optional missing)" if optional_missing else "")
          + ". Ready to build: see AGENTS.md 'From-zero build'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
