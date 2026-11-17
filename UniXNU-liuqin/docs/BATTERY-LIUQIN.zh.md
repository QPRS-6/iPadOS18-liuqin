# liuqin 电池：只读 ADSP/PMIC GLINK 首轮（2026-11-12）

## 输入与范围

- 用户原厂 ROM：OS3.0.7.0.VMYCNXM。
- Android 用户态移植包：piano（小米平板 8 Pro）OS4.0.5.0.XPYCNXM。
  移植包型号不用于选择硬件常量或 ADSP 固件。
- NON-HLOS.bin 已由用户确认来自 **liuqin 原厂 ROM**，原文件不修改。
- battery-plugged.txt：M81_8600mah，94%，Charging，4177 mV，30.7°C，
  CURRENT_NOW=1146000，设计 8600000、满充 6927000、循环 724。
  这些是 Android 样本，不是本移植的设备成功结果。
- 初次只读电量/电压/电流/温度/充电状态，不启用音频、USB gadget、
  充电限流、认证写入、ship mode 或电池校准。

## 硬件及协议来源

本机 liuqin 主线 DTS 的 `adsp_mem` 位于 0x9fd00000，大小 0x3100000。
它与原厂 ELF32 PAS1 固件的 49 MiB relocation span 一致；不能沿用 Nothing
Phone (2) 的 0x85700000/40 MiB。该区域已列入 liuqin 保留内存，仍检查与
XNU 实际 RAM 所有权是否重叠、每个段是否越界，PAS 必须在设备认证签名。

参考 LineageOS/android_kernel_xiaomi_sm8450 的公开源码版本
eff4b40407a1b6f1b1ab84554cbb540bcb495175，
drivers/power/supply/qti_battery_charger.{c,h}：

| 测量 | BATTERY_GET 属性编号 | 转换 |
|---|---:|---|
| 状态 / present | 0 / 2 | 标准 power_supply 状态 |
| capacity | 4 | 百分比的百分之一 |
| voltage / current | 7 / 9 | uV/uA → mV/mA；暂按 Android 充电正值 |
| temperature | 13 | 固件百分之一 °C；Android 导出时除 10 |
| cycle / design / full | 16 / 17 / 18 | full/design 按 uAh → mAh |

Xiaomi M81 的 XM 扩展编号带条件编译，首轮不请求 XM 字段。
Android charge counter 的 6342 与旧公开源码的转换有差异，不能猜单位；
charge counter、TTE、TTF 首轮跳过。未知字段不用于伪造容量或续航。
若实机原始值与此表不符，保留日志并修正，不能把异常值限制到正常范围冒充读数。

## 实现与构建边界

- scripts/extract-liuqin-adsp.py 只读取 FAT16 中的 ADSP 文件，按 ELF 程序头
  拼成 adsp.mbn；不修改或重新签名固件、不计算镜像哈希。
- PONG_POWER_BATTERY_ONLY=1 构建 ADSP、PMIC GLINK、电池、平台资源与背光。
  排除音频图、功放配置和 USB 服务启动。
- PONG_BATTERY_LIUQIN=1 选择 liuqin carveout 与小米电池属性表。
- PONG_CONFIG_PATH 选择此候选的配置，仅将 CONFIG_XNU_PONG_POWER 置 y；
  不修改原本的 liuqin_ipados18_defconfig。
- 保留背光最低档、关屏关闭/唤醒恢复、禁止音量键调光和 touchalive3 保活。
- 新固件放入 OS **副本**，原始 CDR、NON-HLOS 和 queue23 保留。
  固件为只读数据文件，安全认证由 PAS 完成；不发布主机认证成功的结论。

主机 tests/liuqin-battmgr.c 已通过：请求字段、centi-C、电流方向、
percent/full/design/cycles 转换、跳过不确定字段、始终不输出 USB_SET。
内核最终版（含工作线程安全退出及日志）已成功链接，匹配的静默 shim 已组装。
尚无本移植的 ADSP Ready 或电池可用设备观察。

## 候选与下一轮日志

```text
/Volumes/share/pong-ipados18-userdata-32g.queue26.simg
/Volumes/share/shim-boot-pong-perf2-battery26-quiet.img
~/liuqin-unixnu/build/battery26/
~/liuqin-unixnu/logs/battery26/
~/liuqin-unixnu/firmware/adsp-liuqin-os3.0.7/
```

USB 插着可测试，不要求拔线调试 ADB。重点记录 `PongAdsp: stage/failed`、
`PongBattery: PMIC GLINK channel up`、`physical battery power source published`、
SOCraw/tempRaw/currentRaw、超时与错误计数。首次目标是拿到真实回复并显示电量，
不是完整 Battery Health、续航估算或充电管理。

全量 root 会重置 iPadOS Data。回退使用 queue23 + bl-off25-quiet shim。
