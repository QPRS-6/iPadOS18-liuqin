# ArmCpu

Qualcomm SM8475 的 ARM CPU provider，供 Nothing Phone (2)（Pong）及小米平板 6 Pro（liuqin）使用：GICv3 中断控制器与 ARM generic timer，从核由固件 PSCI 启动。

## 适配硬件

- SM8475 八核：1× Cortex-X2（2.995 GHz 请求上限）+ 3× Cortex-A710（2.496 GHz）+ 4× Cortex-A510（1.8048 GHz），4+3+1 三个 EPSS 域
- GICv3 distributor/redistributor，19.2 MHz ARM generic timer
- EPSS 性能状态寄存器 0x17d91000 起，每域一页；只写固件已启用的域

## 仓库关系

上游 [UniXNU/ArmCpu](https://github.com/UniXNU/ArmCpu) 的 Pong 变体。相比上游，这一份面向 Darwin 22/24 重写了 GICv3 与 PSCI 从核启动路径，并新增仅 Darwin 24 编译的 `PongCpuPolicy.h`（EPSS 请求、原生 LUT、4+3+1 拓扑）。

## 说明

- 启动参数 `pong_cpu_policy`：0 关闭投票，1 负载调频，2（默认）每域保持最高的非 boost 请求。EPSS/LMh 仍负责实际电压、频率与热限。
- 编译：`scripts/build-pong-armcpu*.py` 以 `PONG_DARWIN_MAJOR` 选择 Darwin 22 或 24 接口。

### liuqin 显示中核绑定（Darwin 24，services61）

`initCPU` 在每颗 CPU 上读取 `MIDR_EL1/MPIDR_EL1`，确认当前 XNU 逻辑 ID 后发布
`pong_cpu_midr(unsigned)`；0 表示尚未取得该核身份。`fb_mid=1` 时额外打印八核身份。

配合 `fb_threads=3 fb_mid=1`，XNU Mach 层的 `kern/pong_fb_affinity.h` 把显示 presenter
和两个分行 helper 各绑定到一颗实际识别出的 Cortex-A710。必须恰好识别三颗，
并检查目标 processor 已启动、可调度且 recommended；没有可用目标则继续自动调度。
默认 `fb_mid=0`。该选项只支持三个绘制线程；IOKit 调用专用入口
`pong_fb_worker_prepare`，不访问调度器内部结构。

绑定与迁移在显示锁之外进行，`processor_updown_lock` 排除同时离线。
`processor_doshutdown` 在核心退出调度前解绑这些线程；失败、身份不匹配或核心不再
eligible 时，本次启动保持解绑。每个 worker 最多三次实际 CPU/MIDR/MPIDR 样本和
一次回退记录。EPSS 请求由 `pong_cpu_policy` 控制，全局空闲核选择见下文 `cpuq`。

主机回归：`tests/framebuffer-affinity.py --xnu <prepared-xnu> --output-dir <logs>`，
使用实际选择/绑定代码和 XNU 解绑队列代码，覆盖逻辑编号排列、失败、核心退出竞争。
逐像素并行回归见 `tests/framebuffer-parallel-rows.py`。这些检查不代表设备流畅度结果。

机主 `mid3.txt` 已记录 CPU0–3 为 A510、4–6 为 A710、7 为 X2；三个显示 worker
在1/128/1024次任务采样均位于4/5/6核。本次反馈为桌面仍有掉帧、随手写未见黑框。

### QoS 空闲核选择（Darwin 24，services62）

`cpuq=1` 使用已观察到的 MIDR 核类型，结合 XNU effective QoS、QoS promotion 和
scheduled priority 对当前空闲、recommended、同一 pset 的核排序：

- maintenance/background/utility：A510 → A710 → X2。
- 普通任务：A710 → X2 → A510。
- user-initiated/user-interactive 或内核优先级：X2 → A710 → A510。

目标类型繁忙时使用其他可用类型；没有空闲核则回到 XNU 原有运行队列选择。
显式绑定和实时线程走既有路径。`cpuq=0`（默认）保留旧排序。
`PongCpuPlacement.h` 是纯选择逻辑，`tests/cpu-qos-placement.cpp` 覆盖768组
可用核/任务等级、重编号和不可用核回退。显示统计处最多输出三组实际选核计数。

这是 XNU 的 QoS/核类型放置选项；没有移植 Android WALT/EAS 的负载估算和能耗模型。
EPSS 请求策略由 `pong_cpu_policy` 控制。实际流畅度需机主对比。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 实现细节

### 文件

- `ArmCpu.cpp`：`IOCPUInterruptController` 实现。GICv3 distributor 与 redistributor 映射、`ICC_SRE_EL1`/`ICC_PMR_EL1` 设置、SPI/PPI/SGI 分发、IPI 与 `rtclock_intr` 绑定。
- `PongCpuPolicy.h`：仅 Darwin 24 编译。三个 EPSS 域的 0x17d91000 页、原生 LUT 回读、4+3+1 逻辑 ID 映射，以及 `pong_cpu_idle_mach` 的空闲钩子。
- `Info.plist`：`org.blu-sharky.ArmCpu`，匹配 `IOPlatformDevice`，probe score 2000。

### 启动顺序

`pong_cpu_start` 先由板级平台专家调用，完成 GIC 与 timer 后由固件 PSCI 拉起从核；从核进入 WFI 前会调用 `pong_cpu_startup_finished` 通知控制器。Darwin 24 上额外走 `pong_cpu_first_boot` 等待首个 processor 上线。
