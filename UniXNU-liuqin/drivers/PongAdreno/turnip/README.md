# Turnip Darwin 后端

这是 liuqin / SM8475 / Adreno730、iPadOS18.7.10（22H374）的 NativeAdreno ABI1
后端源码。`scripts/stage-pong-turnip-backend.py` 将它放入已准备的 Mesa26.2.6
工作树；`scripts/build-pong-turnip.py --guest-build 22H374` 自动调用该步骤。

## 所有权与接口

- `tu_darwin_connection.cc`：独立连接、隧道选择、固定长度 wire 回复验证、引用计数和 host-sync C ABI。
- `tu_darwin_backend.cc`：每个逻辑设备自己的 fence、BO事务、延迟回收队列和失败状态。
- `tu_darwin_sync.cc`：同设备同步状态域、pending/completed等待、move/reset、ANY与失败唤醒。
- `tu_darwin_iokit.cc`：实际 IOKit open/call/map/unmap/close 和单调时钟操作。
- `tu_darwin_memory.cc`、`tu_darwin_submit.cc`、`tu_darwin_mesa_sync.cc`：Mesa对象与核心的适配。
- `tu_knl_darwin.cc`：物理设备发现、逻辑设备创建及 KMD 表。

临时探测和每个 `VkDevice` 分别拥有连接。正式连接提供实际 VA 范围；探测或
另一个设备销毁不会关闭此连接、重置它的 fence 或回收它的 BO。进程内注册表仅用于
没有设备参数的 host-sync C扩展：恰有一个活动设备时取得带引用的连接；多设备时
返回未覆盖范围，让前端执行原有维护。设备销毁与已经进入的同步调用并发时，连接
保持到最后一个引用退出。

BO的原生handle、GPU范围、CPU映射和token作为同一份所有权记录处理。
分配/映射失败走事务清理；短映射先解除真实VM映射，再释放token。
清理失败时保留记录，失败映射不会成为 Mesa 的成功 `bo->map`。
IOVA只有在原生分配不存在或 `BoFree` 确认成功后归还。设备结束由原生close处理
最后的GPU排空，避免对已经销毁的VMA调用释放回调。

## 锁与提交

提交、回收和同步状态分别有自己的设备锁；注册表锁下不进行 IOKit 操作。
原生提交与最新fence发布在同一锁区间，BO回收在进入回收锁前取得该fence。
回收中的fence轮询使用无递归回收的原始等待入口。

Mesa `vk_queue` 在调用驱动前解析 pending依赖。KMD保持调用者的提交准备锁，
先检查依赖已经pending，再等待真实GPU完成；意外未提交的依赖返回错误。
同步域不使用 Mesa 提交锁。pending不等于completed，ANY等待传播真实错误，
失败广播唤醒等待者。Darwin条件等待使用相对时间，GPU期限使用单调时钟。

已完成fence从设备的8项跟踪表中移除；未完成项不会被挤掉。
其他客户端占满GPU时，按一个总期限等待，不会在几十次短轮询后误判设备丢失。
每批按协商的16/128个IB上限一次提交；超限在任何GPU工作发生前失败，避免在
任意位置切断BR/BV命令序列。空提交继承完整32位fence，包含最高位为1的情况。

默认内核提供32位GPU地址空间、16个连接槽及128MiB次要窗口。
services64的可选 `gpuva=36` 在真实高地址CP验证后提供32槽/256MiB次要窗口；
此后端通过既有ABI1读取实际范围，主机wire回归覆盖36位地址分配/提交/回收。
用户63日志已明确记录128MiB窗口内总空闲足够但连续空洞不足，以及总容量不足两种失败。
图形计算、BGRA格式、GMEM、缓存属性和Native56输入继续由既有实现处理。

## 主机检查

```sh
python3 tests/turnip-backend-core.py --output-dir <logs>/core
python3 tests/turnip-backend-core.py --thread-sanitizer --output-dir <logs>/tsan
python3 tests/turnip-backend-adapter.py --output-dir <logs>/adapter
```

核心以记录型原生IO运行同一份产品代码，覆盖多设备/探测、关闭与导出并发、
事务回滚、真实fence确认后的回收、外部busy、容量、pending/move/ANY和错误传播。
适配层检查失败映射发布、metadata OOM、pending入口契约、调用者锁保留、
原子批拒绝及高位fence。完整iOS交叉编译还检查实际Mesa类型和链接。
这些结果不等同于真机GPU像素、帧率或长期稳定性。
