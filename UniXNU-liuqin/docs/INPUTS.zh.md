# 外部输入清单（新机器 / 新 agent 从这里开始）

这个仓库**只保存自研代码、补丁和构建脚本**。下面这些东西**故意不入库**（见
`.gitignore`），必须在本机准备好：

- Apple 的源码（XNU、IOStorageFamily、libdispatch、libplatform …）
- IPSW 解包、SDK、`mig-138`、trustcache
- 手机固件（Adreno / QCA6490 Wi-Fi / 触摸）
- OS 镜像（`userland-22H374/*.cdr`、`ufs-22H374/*`）
- 构建产物（`out/`、`build/`、`apple/`、`refer/`、`/Volumes/share/build/*`）

所以**新克隆的仓库直接构建一定会失败**，这是设计而不是 bug。先跑自检：

```sh
python3 scripts/check-inputs.py
```

输出里每个 `MISS` 都会给出**该文件是什么、从哪来**；全部 `PASS` 时退出码为 0。
机器可读输出用 `--json`，只看问题用 `--quiet`。

---

## 1. 目录约定

```sh
export LIUQIN=$HOME/liuqin-unixnu          # 所有本地输入都在这里
export DEVICE=$LIUQIN/UniXNU-liuqin        # 本仓库
export UNI=$LIUQIN/UniXNU                  # 共享构建树（./unixnu 在它里面跑）
export SHARE=/Volumes/share                 # 构建产物/日志/待刷镜像
```

`LIUQIN` 和 `PONG_SHARE` 都可以用环境变量覆盖，自检脚本会跟着变。

## 2. 主机工具链

| 路径 | 用途 | 获取 |
|---|---|---|
| `$LIUQIN/bin/python3` | 所有 recipe 用的 python | 装一份 3.11+ 并软链到此处 |
| `$LIUQIN/venv/bin/python` | OS 镜像/打包脚本的 venv | `python3 -m venv $LIUQIN/venv && $LIUQIN/venv/bin/pip install -r requirements.txt` |
| `/Applications/Xcode.app` | clang、codesign、SDK | App Store 安装 Xcode 16 |
| `ld.lld`（默认 `/Users/qprs/liuqin-build3/ld.lld`） | `mkpayload.py` / shim | 任意 LLVM 18+ 的 `ld.lld`，或用 `LLD=` 指定 |
| `$HOME/liuqin-metal` | 只在重编 GPU 栈时需要 | 可选 |

## 3. Apple 输入（不入库）

| 路径 | 用途 | 获取 |
|---|---|---|
| `$LIUQIN/ipsw-22H374/kernelcache.22H374.macho` | 原始内核缓存：kext 闭包、符号、HID 栈 | 从 ipsw.me 下 iPad7,11 22H374 IPSW，解出并 decompress |
| `$LIUQIN/ipsw-22H374/pong-trustcache.bin` | 组件 trust cache | `scripts/prepare-ipados18-*` |
| `$LIUQIN/ipsw-22H374/BuildManifest.plist` | 22H374 身份与组件表 | 随 IPSW 解包 |
| `$LIUQIN/ios-sdks/iPhoneOS18.6.sdk` | arm64 iOS 18 头文件/库/tbd | 从 Xcode 复制，版本必须是 18.6 |
| `$LIUQIN/tools/mig-138/bin/mig`、`libexec/migcom` | 内核 MIG 桩 | 从 Xcode 或 Apple cctools 提取 |
| `$LIUQIN/xnu-10063` | 编译内核用的 XNU 源码 | Apple open source 下载 xnu-10063 |
| `$LIUQIN/xnu-8796` | trustcache 辅助用第二棵 XNU | Apple open source 下载 xnu-8796 |
| `$LIUQIN/UniXNU` | 共享构建树（`./unixnu`） | clone UniXNU |
| `$LIUQIN/UniXNU-sheng` | SM8550 参考树 | clone UniXNU-sheng |

`$DEVICE/refer/ipados18/` 下的 `xnu-11417.140.69`、`IOStorageFamily-323`、
`libdispatch-*`、`libplatform-*`、`AvailabilityVersions-*` 是指向
`$LIUQIN/UniXNU/out/feeds/` 的 **软链接**；新机器上重新指到你自己的
feeds 解包位置即可（`ln -sfn <feed> refer/ipados18/<name>`）。

## 4. 手机相关输入

| 路径 | 用途 |
|---|---|
| `$LIUQIN/firmware/adreno730/` | Adreno 730 固件（zap、gmu、sqe） |
| `$LIUQIN/userland-22H374/pong-ipados18-{system,systemos,data}.cdr` | OS / SystemOS / Data 镜像 |
| `$LIUQIN/ufs-22H374/shngroot-ipados18.bin` | userdata 镜像头 |
| `$LIUQIN/ufs-22H374/ufs-layout.json` | userdata 分区布局 |
| `$SHARE/build/services67/firmware/qca6490/` | QCA6490 Wi-Fi 固件 |
| `$SHARE/build/services49/firmware/novatek_nt36532_m81_fw_csot.bin` | 触摸固件 |
| `$SHARE/build/services67/wifi67.config`、`wifi-profile.json` | Wi-Fi 配置 |

固件都来自**你自己的手机**，从不入库；校验值见
`configs/pong-native-sources.toml`。

## 5. 第三方驱动源码（`refer/`）

`refer/` 被 git 忽略，但构建需要：

```sh
refer/ipados18/QcaBluetooth/     # 22H374 蓝牙组件解包（inspect-ios.py 等）
refer/ipados18/Ath12kWifi/       # ath11k/ath12k 源码
refer/ipados18/linux-7.0.14/     # ath11k 编译所依赖的 Linux 树
```

Linux 树与 Ath12kWifi 是第三方代码（非 Apple）；按需下载对应版本到上面路径。

## 6. `/Volumes/share` 上的构建输入

后续内核/镜像构建会读这几个**历史产物**，请保留：

```sh
$SHARE/build/services80/kernel/va36/boot-args.txt   # 每次构建的 boot-args 基线
$SHARE/build/services97/userland/{bluetoothd,BlueTool}  # 蓝牙 runtime（pong-bt97）
```

其余 `servicesNN` 目录可以删（都是可重建的中间产物）。

## 7. 第一次构建

```sh
python3 scripts/check-inputs.py            # 必须全 PASS
cd "$UNI" && rm -rf "$LIUQIN/device-tree"  # 每次内核构建都从零打补丁
# 然后按 AGENTS.md 的 “From-zero build” 顺序：
#   1) kernel  2) shim  3) Metal（可选）4) root 镜像 5) userdata simg 6) fastboot
```

完整命令见 `AGENTS.md` 与 `docs/BUILD.zh.md`；目录地图见 `docs/LAYOUT.zh.md`。

## 8. 常见“缺文件”症状对照

| 现象 | 缺的东西 |
|---|---|
| 内核构建一开始就报 patch/源码不存在 | `$LIUQIN/xnu-10063`（或 `xnu-8796`） |
| `cannot find -l...` / tbd 报错 | `ios-sdks/iPhoneOS18.6.sdk` 或 `mig-138` |
| `mkpayload.py` 找不到 `ld.lld` | `LLD=` 没设或路径不对 |
| 蓝牙构建报 `inspect-ios.py` 缺失 | `refer/ipados18/QcaBluetooth` 未解包 |
| Wi-Fi 构建报 ath11k 头文件缺失 | `refer/ipados18/{linux-7.0.14,Ath12kWifi}` |
| `build-ipados18.py` 报 OS/Data 镜像缺失 | `userland-22H374/*.cdr` 未准备 |
| simg 生成报 header 缺失 | `ufs-22H374/shngroot-ipados18.bin` |
| 构建写不进去/镜像看不见 | `/Volumes/share` 未挂载 |
