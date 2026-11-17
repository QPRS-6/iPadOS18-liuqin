# liuqin Oplus 服务工具静态还原

对象为机主提供的 `ci_liuqin_coloros_system_release_module/oplus_system`。
这是未去符号的 ARM64 Android 用户态 ELF，原编译单元名 `oplus_liuqin.c`，
不是内核模块、触控固件或原生 SPI 驱动。没有运行程序、安装模块或修改设备节点。

## 已确认的行为

| 入口 | 实际操作 |
|---|---|
| `pen 1`（任意非零参数） | `/dev/xiaomi-touch` 的 ioctl `0x5400`，初始化参数前缀 `{0,20,18}` |
| `pen 0` | 同接口、同请求，参数前缀 `{0,20,2}` |
| 两种 pen 操作 | 都执行 Android `settings put global force_pad_floor_fps 1` |
| `dt2w v` | 同 ioctl，参数前缀 `{0,14,v}`，v 直接来自 atoi |
| `pen_soc` | 只读 `/sys/class/qcom-battery/pen_soc`，最多 31 字节后打印 |
| `battery 1` | 分别向 authentic、slave_authentic、verify_slave_flag 写一个字符 `1` |
| `battery 0` | 同三个节点写字符 `0` |

三个电池节点位于 `/sys/class/qcom-battery/`。程序没有实现签名认证、校准、PD
协议协商或电流调节算法；这里只能确认节点写入，具体内核副作用须另查对应 handler。
不得当成安全快充方案照抄。当前移植的电量/充电工作仍保持既定只读边界。

`service.sh` 在启动等待 20 秒后按 battery/pen/dt2w 顺序各调用一次。这不是常驻
守护进程。ELF 的 main 最终返回 0，pen/dt2w 不检查 open/ioctl 结果，因而脚本的
成功退出码不证明硬件操作成功。二进制只证明前三个 int32 参数被初始化；完整
ioctl 用户缓冲区长度/内核 ABI 仍需内核源码或对应模块，不能据此前缀写不安全调用器。

反汇编依据：pen_mode `.text+0xbfc`、参数选择 `+0xc08/+0xc24`、rodata `{0,20}`
`0xec0`、ioctl 调用 `+0xc48`；dt2w_mode `+0xc70`、rodata `{0,14}` `0xec8`；
battery_mode `+0xa98`；main `+0xccc`。PLT 与 `.rela.plt` 已交叉核对 libc 调用。

## 与原 liuqin 厂商驱动的交叉还原

参考之前提取的未 strip `nt36532_touch.ko`，而不是假定该 ROM 安装了同一版本：

1. `nvt_set_cur_value` 的模式 20 分支进入笔的引用计数/Generation-one 屏蔽逻辑。
   值 18 的低四位为 2、且 >=16，增加 `pen_count`；值 2 释放一次请求。
2. 未屏蔽且计数非零时，置软件 `pen_bluetooth_connect` 为真。
   **这不建立真实蓝牙连接**，仅改变厂商驱动的启用状态，解释了模块为何能强制开笔。
3. `update_pen_status` 在触摸 awake 时，按软件连接、笔充电连接和游戏模式门控启用值，
   串行化状态、等待 35 ms，再选事件页并发送 `{0x50,0x7b,enabled}`。
   CTP_SPI_WRITE 加 bit7 后，线上地址字节是 `0xd0`；不能把 write mask 误当另一页地址。
4. 重复 `pen 1` 可增加引用计数；`pen 0` 不一定立即清空所有引用。它不是幂等开关。

这些字段的名称已按模块保留的日志字符串交叉核对。关键地址：模式分支
`.text+0x8e3c/+0x8f88`，计数修改 `+0x8fa0/+0x8fc4`，连接状态计算 `+0x90b0`，
更新调用 `+0x9178`；固件命令编码/发送 `update_pen_status+0x334/+0x34c`
（`.text+0x2f9c/+0x2fb4`）。

## 新原生驱动采用的范围与限制

- 机主选择：从零做原生 GENI/GPI SPI，无 GPIO SPI fallback；旧驱动保留用于回退，
  新旧实现不得同时访问控制器。背光、真实 Metal、电量和原 queue26 root 不动。
- 笔为小米触控笔第二代。按机主说明，正常书写/压感由面板接收，不把蓝牙作为前提。
- 确定的参考是最终面板笔配置与状态约束，不是 Android 节点、计数副作用或伪造连接。
  固件启用配置、真实笔报文解析和 iPadOS digitizer 接入分别验证，不能启用后就声称修好。
- 主线项目当前不支持手写笔，其资料不能证明二代笔压感/按键 ABI。尚需读取厂商解析
  路径及设备真实报文，未知字段保留未知，不冒充 Apple Pencil 服务或能力。
- 原生 GENI/GPI、独立 SMMU 映射和真实 IRQ 接入已实现传输试验，native33-r2 主机
  编译成功；尚无原生 DMA 真机日志，固件初始化、报告 IRQ、触摸/笔输入还未接入。
  **候选仅验证传输，不是可用的新原生触摸／笔镜像**。

检查用的完整伪代码、汇编、重定位与字符串清单保存在本机
`/Volumes/share/tmp/services33/oplus-analysis/`，伪代码为 `.c.txt`，没有编译或运行。
