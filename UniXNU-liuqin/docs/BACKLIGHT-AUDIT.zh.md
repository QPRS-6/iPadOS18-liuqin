# liuqin 背光检查（2026-11-11）

## 已有设备证据

- KTZ8866 读出 level=1009，音量键可以调光；硬件路径不是当前滑条失效的主要缺口。
- 发布 Apple 类名或背光匹配属性的试验造成卡顿/崩溃。裸服务可用，但不能据此确定 CoreBrightness 内部的降帧原因。
- queue20 的 die.txt 有进程/Metal 初始化记录，但没有 swap；启动回归的精确调用栈尚未拿到。

## 确认的代码缺陷

1. 多个 Objective-C 类按同一个 SEL 查原 IMP，会用错接收者；第三个对象参数也曾按整数调用。宽泛钩子现已退出运行路径。
2. `IOServiceGetMatchingService` 消费匹配字典，原代码随后再 CFRelease 属于重复释放。
3. IOConnectSetCFProperties 调用 user client 自身的 setProperties，不自动转发 provider；已补转发及 clientClose。
4. 把归一化值、百分比和绝对档位混用，没有依据；BKS 入口现在只接受有界归一化 float。
5. 写后回读不一致曾返回成功；现在报告失败。UI 读回使用已接受的目标值，硬件错误会撤销目标。

## 找到设置滑条的实际路径

本机 22H374 的 DisplayAndBrightnessSettings 导入 BKSDisplayBrightnessSet、
BKSDisplayBrightnessGetCurrent。BackBoardServices UUID 为
3a438d91-42c7-3a13-b8a7-8aef17b697c7。

- Set 相对镜像偏移 0x3ea40，s0 为 float，x0 为 transaction 指针。
- GetCurrent 偏移 0x74f4，s0 返回 float。
- 两处前 16 字节都是寄存器/栈操作，可重定位。安装前检查 UUID、可执行范围及原始指令。
- 新入口仅在 Preferences / SpringBoard 安装，硬件通道拒绝时回到原函数。
- 不再主动创建 CoreBrightness 客户端轮询，也不再普查或钩内部服务端。

## 仍须完成，不能声称已彻底修好

- 并发请求/定时器：pending 与 request 是分开的原子值，多个生产者仍可能相互覆盖。
  应统一为单一请求快照，并让工作循环独占定时器的调度。
- I2C 部分写入失败后，应重新读取实际亮度再更新状态；不能只沿用旧缓存。
- IOKit 连接的并发打开及 UI 读回失败路径还需要收敛。
- BKS 写/读新入口尚未经过设备验证；控制中心也必须单独验证，不能由设置成功推断。
- 原宽泛钩子暂保留源码但已不执行；后续应在有回归测试后删除。

以上为审查时的缺口，收尾结果如下；设备测试仍不能由主机构建代替。

## bks23 候选收尾结果

- pending/request 合并成单个带 generation 的 CAS 请求快照；快速按键相对累加。
- 外部线程不再调用定时器。只有工作循环每 16 ms 重新调度并消费最新请求。
- 部分写入失败也读取实际亮度；只有请求版本未被替换时才撤销目标。
  读取失败或 FIFO 未退休则停止驱动，sysctl 和 user client 返回 NotReady/ENXIO。
- IOKit 连接操作串行化；补齐 user client 的写属性转发、关闭和目标值查询。
- 原函数 trampoline 在替换入口前发布，不再有包装函数先运行却没有原函数的问题。
- 补齐同一 UUID 的 SetWithImplicitTransaction（0x3ea94）及
  IsBrightnessLevelControlAvailable（0x3ec34），仍检查实际硬件通道是否就绪。
- 保留 Metal/GMEM/alpha blend，恢复 touchalive3 的保活启动参数。
- requests 主机测试通过：边界、渐变时长、旧失败不覆盖新请求、4 线程相对累加。
- 内核和用户态已编译链接；UI dylib 按 4096 字节签名页签名。
- 全量候选通过 OS CDR 副本组装，原始 CDR 和旧版 simg 均未覆盖。
  未进行哈希、设备回读或性能测试。

候选：

```text
/Volumes/share/pong-ipados18-userdata-32g.queue23.simg
/Volumes/share/shim-boot-pong-perf2-bl-bks23-quiet.img
~/liuqin-unixnu/build/backlight23/deployment.json
~/liuqin-unixnu/logs/backlight23/{kernel,root,shim}.log
```

本机没有挂载 /Volumes/PongDev；此次沿用 liuqin 本地构建目录及 share 交付目录。
静默仅针对 shim；XNU 的诊断控制台和真正的 panic 处理保留。

验证必须同时覆盖：桌面正常启动、设置滑条调光、控制中心不弹回、音量键、
快速连续操作、最低/最高亮度、关屏再唤醒及硬件回读失败。
