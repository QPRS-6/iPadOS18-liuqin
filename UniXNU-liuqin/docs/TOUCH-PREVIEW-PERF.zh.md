# 触摸、白色预览与低帧率排查（2026-11-12）

## 证据及已做修改

- 旧 16.txt 在 780 秒内记录 48 次固件下载；下载占用触摸 SPI，报告线程在
  tch_fw_active 时停止读取。因此重下载风暴本身会造成长时间输入中断。
  这不是最新 battery26 的观察，不能据此认定新版本也有同样频率。
- 原恢复逻辑在 300 秒无 contact 后重下载，但空闲/按住不动也会没有新 contact。
  新增 touch-recovery-policy.patch：默认关闭无 contact 的定时重下载；可显式
  liuqinidle_recover>=300 打开旧兜底，真正错误/状态失效的恢复路径保留。
- 原中断 streak 未检查 FD/FE 签名，任意无效包也累计至 3 次后重下载。
  改为只计 6 字节 FD/FE 的 watchdog 签名、10 个非自激中断。
- 原主机 poke 宽限按读取次数递减，实际时长随轮询/负载变化，且在 poke 后才设。
  改为 poke 前设置绝对时间的 1 秒宽限；poke 与 SPI 事务串行，下载时跳过。
- 握手清理原先一生只允许 8 次，之后无法清理；移除终身预算，失败读数 0xff
  不触发写入，仍仅在非零状态时使用原有有界清理操作。

这些针对误恢复，不声称已经消除真正的固件故障；仍需停摆日志验证。

## CPU 合成第一步

blend-row-fast.patch 为未缩放 BGRA 行添加 4 像素一组的 opaque copy / transparent
skip；混合 alpha 保留原逐像素公式。不使用内核 SIMD，不修改通道、色深、模糊、
GPU 提交及 fence/coherence。

tests/blend-row.py 对实际提议的函数做等价比较，覆盖 0/1/3/4/5/17/1800 宽度、
opaque/transparent/mixed alpha 及目标行边界，已通过。尚未测设备帧率收益。

## 白色预览：尚未定位

当前不能把 App 切换器缩略图和壁纸预览都归因为 AppleM2ScalerCSCDriver 缺失，
也不能仅凭 shader capability 的无答案日志断言根因。需区分：

1. 预览渲染进程是否退出、jetsam，或设备/VA/纹理分配是否失败；
2. 离屏附件、store/resolve 是否实际写入；
3. 纹理→buffer/IOSurface 拷贝与真实 fence 是否完成；
4. CPU 读回是否可见、布局/stride/format 是否一致；
5. 图像已有内容但预览服务是否丢弃或显示了占位图。

不盲目放大 VA 租约、不伪造 Metal 能力，不套用曾导致颜色跳变的 GMEM 交换补丁。
最终 GPU 路径继续为真实 Metal→metal2vulkan→Turnip→Adreno 730，无 CPU 渲染替代。

## 低帧率所需新证据

连续滚动、App 切换器、壁纸预览各采一次；触摸停摆时测试按键响应，并保留完整
ponglog、屏幕截图和 root/shim 名称。用实际帧数及 GPU/CPU/等待时间区分：
没有提交（合成器状态/客户端故障）、排队、GPU 执行、BO 同步或 CPU 合成耗时。
不能用旧日志的低输出帧数直接推断新的 GPU 性能，也不承诺固定目标 FPS。

新增两份补丁此前在准备树 git apply --check 通过；perf27 的主机构建结果见下文，
perf27 的设备日志现为 27.txt，见下文；主机构建和设备观察分开记录。

## 26.txt 设备观察与 perf27

- 约 972 秒：24 次恢复公告、25 次固件下载（CRC 清理行不能重复计作恢复）。
  SPI 71611 次读取无失败，contact frames 一直为 0；这不能证明用户没有操作。
- ADSP 到 stage 13，PMIC GLINK up，发布了真实 86%/4149 mV 的 power source；
  后续 SOCraw 从 8600 降到 8300，电池协议未见超时或错误。
  放电状态 2 下 currentRaw 为正，IOKit 电流方向还需另行核对，不能说全部电池功能正常。
- swap 文字仅 5 条，但代码只打印最初 8 个 swap，不能按日志行数计算总帧数。
  无连续帧率统计，不能计算实际滚动 FPS。
- UI Metal 的两轮遥测均为 c2/0 d2；K1/K2 的 commit-to-callback 为 6888/12207 us，
  age 为 103174/67198 us（包括调用者空闲，不等于纯编码耗时），也不等于 GPU 执行时间。
  日志另有 cameracaptured 的 Pong Metal slow / Pong GPU stages：其首次 commit 无
  render pass/draw，run_us=69902；不能把这个初始化样本当作 UI 帧耗时。
  先前只搜 [gpu slow] 漏掉了这组不同名称的遥测；两条 UI 命令仍不足以判断滚动瓶颈。
- 按日志的 shared-cache slide 0xe630000 解码，SpringBoard 主线程的多次相同
  调用栈涉及 libsystem_kernel/libsystem_platform、CoreServices 和
  ControlCenterServices；backboardd 主线程涉及同步 XPC、Foundation、CoreServices。
  这提示 LaunchServices/元数据链路阻塞，但尚未取得锁持有者的调用栈。
- 无 SpringBoard/backboardd/预览进程退出证据；default.metallib 的 binary archive
  InvalidFile、若干 Metal 能力无答案和 M2Scaler 匹配失败目前只是线索，不是根因结论。

## perf27 主机构建结果

- 修正新增触摸补丁中的时间单位：`kern/clock.h` 包含 `mach/clock_types.h`，
  当前 XNU 提供 `NSEC_PER_MSEC`，不提供 `kMillisecondScale`；宽限仍为 1000 ms。
- 从干净的生成目录构建，命令为 `unixnu build kernel -j 8 -D UniXNU-liuqin`，
  使用 battery26/battery.config、PONG_POWER_BATTERY_ONLY=1、PONG_BACKLIGHT_KTZ8866=1；
  配方传入 --skip-image-verification，未另跑镜像哈希、审计或性能测试。
- 源码基于设备分支 b68654b 加现有未提交修改；XNU 11417.140.69 的固定版本为
  43a90889846e00bfb5cf1d255cdc0a701a1e05a4。
- 内核与 quiet shim 均构建成功。镜像：
  `/Volumes/share/shim-boot-pong-perf27-touch-quiet.img`（59158528 字节）；
  候选与 deployment.json 在 `/Volumes/share/build/services27/`，
  日志在 `/Volumes/share/logs/pong/services27/`。启动参数含 NUL 共 985 字节。
- 仅加入触摸恢复策略和等价 BGRA 行快路径，继续配 queue26 原 root；
  未修改 Metal 用户态、电量计/背光/关屏配置，未刷任何分区。
  本机没有可用 fastboot/ADB 命令，临时启动由机主执行。
- 白色预览仍未修复；只读审查表明 runtime 的 synchronize_resource 对 unified/shared
  路径可以不录额外命令，所以前端空实现本身不是白屏证据。需要实际 storage mode、
  附件写入和完成后像素证据再决定是否修改。
- 后续若扩大 lsd 采样，先修现有 sampler 持 task_lock 读取用户栈的问题；
  当前 perf27 未扩大采样范围或启用跳过 LS recovery 的用户态 hook。

## 27.txt：已确认空闲后重新操作完全无响应

- 设备内核版本时间对应 perf27；启动参数保持 battery26 配置。
  日志有 3 次固件下载（初启 + 两次 watchdog 恢复）。两个恢复都打印 revive 1，
  因为接触报文会清零计数；不能按最高编号认为只恢复了一次。
- 初次恢复后开始报点，heartbeat 24/36/48/60/72 s 的 contact 计数分别为
  115/204/560/602/641。84 s 报告区变 FD；第二次恢复后先为 FF/00，108 s 起又为
  固定 FD，直到 heartbeat 300 s 仍为 641。SPI 成功读取增至 22090，错误为 0。
  这些 heartbeat 标签由线程周期累计，包含下载期间的停顿差异，不是精确 uptime。
- 机主明确确认：空闲后反复点击/滑动完全无响应，不是仅仅放着没操作。
  因此这段是报告链路失效；不能用“正常空闲”解释，也不能据 state running 声称扫描正常。
- 代码缺口：watchdog 下载后无条件清除 tch_fw_dead；controller 仍返回 0xa0/0xa3，
  却没有新的 IRQ 时，普通 running 分支阻止进一步恢复。perf27 去掉 idle-only 重下载
  避免了误恢复，但未覆盖“已确认故障之后恢复未完成”的情况。
- 真正停止报点的触发原因还未证实。不得把 watchdog FD/FE 等同于已证明 CRC 失败，
  不得再把定期重下载当作“不停摆”的最终方案。旧 iOS16 的 no-poke 实验也曾在活动中
  停止上报；关闭 INT poke 不是已经验证的根治。

### perf28 恢复兜底和故障证据

- recovery-confirm.patch：仅在已确认 watchdog 恢复后进入待确认状态。真实、边界内的
  接触记录或正常全 FF 空记录区可解除待确认；启动 FF/00 占位或 FD/FE 填充不可。
  若 FD/FE 持续且未收到正常报文，则无需新 IRQ 也能重试；间隔为下载完成后
  15/30/60 s，之后保持 60 s；已知故障的重复 IRQ 同样遵守间隔。
  普通无人操作不进入这个重试状态。全 FF 的协议确认不等于已验证随后必能触摸。
- fault-history.patch：按 liuqin 自己的 nt36532_touch.ko 核对：
  nvt_ts_work_func 的 0x686c/0x687c 从 mmap+0x74/+0x78 读取地址并传给 nvt_read_fw_history；
  NT36532_cascade_memory_map 对应 0x121afc/0x121b3c。每块只读 64 字节，最多 8 次捕获。
  在 0x51 ack/复位/下载前保存历史及状态、CRC latch、GPIO、时间；原始事件码尚未解码。
  不改固件、供电或充电参数；记录用于追根因，不声称此探针能阻止固件故障。
- 直接反汇编厂商 nvt_clear_fw_status 确认偏移确为 0x51；nvt_fw_crc_enable 的 SPI 地址
  字节为 0x50（上线经 write-mask 成 0xD0），不能误把写标志位当成另一页的地址。
  当前 tch_fw_reg_write 显式用 EVENT_BASE 选页，掩掉 reg 的 bit7，因此这个常量的命名
  虽易误读，现有写事务并未因它发到错误页；没有据此做无证据的地址修改。

### 预览与帧率的新证据（未在 perf28 修改 GPU）

- MercuryPosterExtension[293] 有 230 条 DeviceLost 错误（不是 230 次独立 GPU 故障），
  KaleidoscopePoster/Unity2025Poster 有 OutOfMemory(Device)。AegirPoster 多次 SIGABRT，
  KaleidoscopePoster/Unity2025Poster 有 SIGSEGV，SBRendererService[396] 有 SIGABRT。
  这些比“缺少 scaler/能力警告”更直接，但内存失败是 VA、BO、真实页或其他资源耗尽
  尚需定位；不能无证据扩大租约，也不能认定所有白色预览都由同一退出导致。
- 日志有连续显示驱动统计：前段若干窗口约 13–31 帧/秒，驱动拷贝/合成约 3.8–10 ms，
  等 GPU 约 3–25 ms。[gpu slow] 还显示 poster 作业 GPU 部分约 44 ms，backboardd
  随后的 queue 部分约 22–32 ms。它们是被筛选的慢样本，不是总体均值。
- 后段 72–86 帧/秒的窗口在 display power off 日志之后，不能直接作为亮屏滚动成绩；
  场景/触摸/进程活动与 queue26 不同，仍不能量化 BGRA 快路径的单独收益。

### perf28-r2 最终主机构建

- 内核及 shim 已构建成功，`build_complete=true`、`returncode=0`；生成源码包含
  原子 CAS 的八次故障捕获预算。未上机，未运行另行镜像审计/哈希或性能测试。
- 试验镜像：`/Volumes/share/shim-boot-pong-perf28-touchdiag-r2-quiet.img`，59158528 字节。
  构建记录与 deployment.json 在 `/Volumes/share/build/services28/final/`，
  日志在 `/Volumes/share/logs/pong/services28/final/`。
- 配 queue26 原 root，启动参数含 NUL 985 字节；电池、背光、GPU、INT wake 参数
  与 perf27 相同。不需重刷 root。首次未加原子预算的 perf28 中间产物不作为试验推荐。
- 下一次只做一轮：先操作确认触摸，再空闲 2–3 分钟后重新点/滑；若停摆，保持日志
  抓取条件并给完整 ponglog，以原始 fault/history 行确认停止报点的触发原因。

## 28.txt：恢复可重试，但仍未消除停摆

- 内核版本时间为 2026-11-12 12:19:43，匹配 perf28-r2；完整日志记录 6 次固件下载
  （初启 + 5 次恢复），不是按最大的 revive 2 算两次。下载期间约 7–17 s 不能报点。
- contact 计数在 411 停住后，第一次恢复未产生正常报文，`watchdog-retry` 分支确实
  执行；第二次恢复后 contact 从 411 到 437，之后再次恢复后从 443 增到 1097。
  最后 heartbeat 240 s 有 17650 次成功读取、0 错误。证明恢复兜底跑过且能重新报点，
  不证明触摸已经持续稳定；`recovery confirmed` 也可能先由正常空报文触发。
- 捕获 5 组 fault/history，时间戳分别约 58.9/144.8/170.7/212.8/248.8 s；全部
  state=a3、CRC latch=00。没有锁存 CRC 重启循环的证据，不能把 FD 等同于 CRC 错误。
- 历史区确实有内容和跨恢复变化：第一组 bank0 为杂值，后续 bank0/bank1 主要为 04，
  间有 21 ff、2a/e3，末尾为 01 04 0c f9/fa/fb/fc/fd。厂商 nvt_read_fw_history
  本身只打印原始字节，不给这些事件码的定义；不能臆造为“04=某个硬件故障”。

### perf29：只改变完整报文读取长度

- 重新核对 liuqin 厂商模块 `.text+0x6710/0x6714`：CTP_SPI_READ 的 length=109。
  该 helper 在 `.text+0x4d0` 设置总传输 length+1，在 `.text+0x640..0x650` 将
  RX+2 的 length-1 字节拷贝到调用者 buffer+1。因此实际数据为 108 字节，总线为
  地址 + dummy + 108 = 110 字节；不是“109 个数据字节”或“18 根手指”。
- 当前驱动常规报告只读 65 个数据字节、总线 67 字节；这是可以证实的厂商事务差异。
  它是否导致停摆还没有设备对照结果，不能称已找到根因。
- full-report.patch 新增一次完整 108 字节读取，将原先的前 65 字节复制给现有解码器；
  常规报告和 watchdog ack 后的报告重读使用同一 helper。没有第二次重复读，没有扩展
  fingers 上限、生成笔事件或改变坐标算法。读取失败不复制未初始化数据。
- 只有这一个协议变量变化；保留 perf28 的恢复限频、故障历史、INT wake、背光、电量
  与真实 Metal 配置。更长事务可能降低轮询速率，需看下一份日志，不能预先宣称性能提高。
- 已构建成功的镜像为 `/Volumes/share/shim-boot-pong-perf29-fullreport-quiet.img`，配原 queue26；
  构建记录 `/Volumes/share/build/services29/`，日志 `/Volumes/share/logs/pong/services29/`。
  内核 returncode=0，shim 59158528 字节；参数含 NUL 985 字节。配方跳过镜像验证，
  未另跑镜像哈希/审计或性能测试。仍无“不停摆”或白色预览已修复的设备结论。

## 29.txt：完整读取没有根治停摆

- 设备日志确认 `full report payload=108 wire=110 prefix=65 one-read` 路径执行。
  初启 + 4 次恢复共 5 次固件下载，4 次均为 watchdog-before-ack；恢复后触点继续增加，
  最后 heartbeat 216 s 为 2184 contact、14144 成功读、0 失败。无 panic。
- 故障时间约 62.3/158.7/215.1/254.5 s，仍是 state=a3、CRC latch=00，历史仍主要
  为 04 与 21 ff。前两次故障早于第一条 display power off，不能仅归因于关屏。
- 相比 28.txt 的 5 次恢复，少一次不证明改善：时长、操作、关屏时段不同。
  完整读报文的假设未得到“不再停摆”的支持，不继续凭此盲调长度或恢复阈值。

## 机主提供的 xiaomipad-6pro-mainline 参考（2026-11-12）

主项目固定到 `0d71353750bdf1ff7724cbb4d5672914d4739e42`，其 kernel/source.json
钉住 `yzddmr6/linux-sm8450-liuqin` 的 `59413493369901ffc7bae4b2be707e2b0d511673`
（liuqin-6.17）。这里是源码比较；作者的 Linux 设备可用记录不等于当前 XNU 结果。

### 最直接的明确错误：自动复制完成检查选错寄存器

- `drivers/input/touchscreen/nt36523/nt36xxx_mem_map.h:532–537`：NT36532 为
  hw_crc=2、auto_copy=CHECK_TX_AUTO_COPY_EN（2）；mmap 的 TX_AUTO_COPY_EN=0x1fc925，
  SPI_DMA_TX_INFO=0x1fc914。
- `nt36xxx.c:292–300,1203–1246`：模式 2 发 0x56，然后等 TX_AUTO_COPY_EN 清零；
  模式 1 发 0x69，然后才等 SPI_DMA_TX_INFO 清零。不能按 hw_crc 数值混用完成寄存器。
- 交叉核对 liuqin 自己的厂商模块：NT36532_hw_info 的首三字节 `02 02 01`；
  nvt_tx_auto_copy_mode 在 .text+0xeb8/+0xec0 选择模式 2 的 0x56，
  nvt_wait_auto_copy 在 .text+0x13c8/+0x13e0 调 nvt_check_tx_auto_copy；
  后者读 mmap+0x8c 并在 .text+0x122c 判断清零。与主线实现一致。
- 当前移植确实发了 0x56，却检查 0x1fc914；29.txt 每次都是 txinfo=03@200，
  超时后仍发 CRC/BOOT_RDY 并称 BOOTED。这是明确的控制流程错误，
  但仅修它是否消除后续停摆还需设备结果。
- autocopy-status.patch：只改为检查 0x1fc925 的成功读数 00，200 次有界轮询；
  失败恢复 bit-bang pace 并返回错误，不再跨过复制前提继续发 CRC/start 命令。
  新日志带 mode/reg/status/polls/complete 或 timeout。不改变固件、读取长度、INT wake、
  GPU、背光或电池行为。候选 perf30 已完成主机构建，仍配 queue26 原 root；
  镜像 `/Volumes/share/shim-boot-pong-perf30-autocopy-quiet.img`，59158528 字节，
  参数含 NUL 985 字节。构建记录 `/Volumes/share/build/services30/`，
  日志 `/Volumes/share/logs/pong/services30/`。跳过另行镜像哈希、审计与性能测试，
  后续设备日志为 30.txt：完成检查通过，但没有触摸，见下文；没有“不再停摆”的结论。

### 不能直接照搬的面板/触摸生命周期

- 该板的 DTS 把 NT36532 触摸连到 DRM panel follower：共享 TDDI 的 GPIO0 面板复位
  会丢弃 RAM 触摸固件。触摸在 panel_prepared 后加载固件、等 RESET_STATE_REK，再开启 IRQ。
  unpreparing 先停止输入/工作队列，避免与面板复位竞争。
- follower 禁止在已由面板接管的 TDDI 上做独立 engineering reset；ESD 路径会等待
  下一次真实面板 cycle。当前 XNU 沿用 ABL 的 DSI 初始化，关屏主要是背光控制，
  不能直接移植“停输入直到 panel prepare”，否则可能永久等不到这个事件。
- SPI4、CS0、9.6 MHz、GPIO21 rising、swrst=0x1fb43e 和原 CSOT firmware-name
  可核对板级事实；硬件 SPI/GPI DMA 不能靠移植 GPIO bit-bang 的 pace 值冒充。
- 该主线驱动仅读 65 字节 touch 数据或 79 字节 touch+pen 数据，因此
  “商用模块读 108 字节”不是所有工作实现的必需条件；29 的完整读取对照应保留阴性结论。
- 参考没有提供 04/21 ff 故障历史码的定义，不能由这一仓库臆造出固件错误原因。

## 30.txt：复制完成通过，初次启动无报点且没有恢复

- 机主确认此次无触摸。设备日志确实执行 `fw auto-copy mode=2 reg=1fc925 status=00
  polls=0 complete`，没有复制超时；固件 BOOTED 后状态到 a3、FWINFO 合法，报告线程启动。
  所以不能把此次无触摸说成新的完成检查把启动卡住。
- 仅有初始 1 次固件下载；0 次恢复，0 组 fault/history。最后 heartbeat 252 s 为
  16624 成功 event reads、0 失败、2 changes、0 contact，报告区固定 FD。
  用户操作失败不能解释为“没碰屏幕”；SPI 成功不等于控制器仍在报点。
- 日志只记录 6 个 GPIO21 上升沿（日志上限尚未用完），没有触发 watchdog 门槛。
  这不证明实际线上仅有 6 个硬件脉冲，轮询可能漏采。
- 恢复确认此前仅在 watchdog 下载后开启；初始下载后 recovery_pending=false，
  而 running/a3 分支使无 IRQ 的初始失败一直不进入恢复。这是可对应日志的启动空档。
- 单次 perf30 现象比 perf29 差，但不能由此证明修正复制状态寄存器本身造成了扫描停止；
  相似的首次无报点现象在更早版本也出现过。自动复制与“正常报文/触摸可用”仍须分开。

### perf31：把初次启动也纳入正常报文确认

- startup-confirm.patch 只改变初始确认状态：报告恢复线程开始时令 pending=true，
  baseline=0 计入此前已读取的正常报文，并设置 15 s 的有界初始确认窗口。
  正常全 FF 空记录或边界内接触报文可确认启动，不要求用户必须在开机时持续触摸。
- 未确认且最新成功报文仍是 FD/FE 时，沿用已有故障历史和 15/30/60 s 限频恢复，
  不再要求初始阶段出现足够 IRQ。单纯没人触摸不会触发重下载。
- 这是初启恢复兜底，不是停止报点根因的修复；既有正常报文判据也不是未来触摸
  必能响应的证明。保留 perf30 的正确 copy mode 2 完成检查，不添加随机笔命令、
  不改固件/面板复位/背光/电量/GPU，不重刷 queue26。
- 候选 `/Volumes/share/shim-boot-pong-perf31-startconfirm-quiet.img` 已完成主机构建，
  59158528 字节、参数含 NUL 985 字节；记录 `/Volumes/share/build/services31/`，
  日志 `/Volumes/share/logs/pong/services31/`。未上机，未另跑镜像哈希/审计或性能测试。

## 31.txt：输入可用，但仍反复恢复

- 设备日志匹配 perf31；初始 `recovery confirmed ... after 0 attempts`，之后有真实触点。
  这次没有执行初启 FD 重试，所以不能把开机可用归因于已验证修复 perf30 的初启失败。
- 全日志 6 次固件下载（初启 + 5 次恢复），全部自动复制检查 status=00/polls=0。
  后续 4 次由 watchdog 触发、1 次是未确认恢复后的重试；最后 heartbeat 300 s 有
  1897 contact、20298 成功读、0 错误。无 panic。证明输入及兜底运行，不证明“不停摆”。
- 故障时间约 79.0/218.8/242.7/285.3/340.7 s，均 state=a3、CRC latch=00；历史仍主要
  为 04/21 ff，代码没有提供它们的语义，不能以字节模式臆造 CRC/供电/总线故障。
- 主线 reference 的正常输入是 IRQ 驱动的 SPI，而当前是 GPIO 模拟 SPI 持续轮询。
  软件传输函数返回 0 不能证明字节内时序/电气波形正常。不能再通过增加重置兜底声称根治。
- 已只读复核背光：常规 writeLevel 改 BlEn bit6 与亮度寄存器，不在关屏时拉 GPIO6
  全局使能或修改 LCD bias 寄存器。31 的关屏/报点停止有时序关联，但 29/30 也有关屏前
  失效，不能把背光关屏认定为唯一原因，也没有据此修改已工作的背光。

### perf32：只增加传输时序与命令窗口诊断

- spi-timing.patch 记录正常报告读取的持有总线后耗时，以及每次 bit-bang 调用内的最大
  字节时间间隔；不改读取长度、pace、IRQ mask、触点解析或恢复条件。
  开始时间在获得 SPI owner 后，所以不包含等待总线锁的耗时。
- 字节测量包含一个字节的实际发送以及相邻循环开销，是 CPU 时间观察，**不是**实测
  SCLK 边沿间隔，更不是直接证明违反芯片时序。不可把一个大数自动解释成根因。
- 保留 last / good / first_fd 的报文编号、完成时间与耗时，以及正常报告读取的峰值。
  good 仍只代表原有正常前缀/全 FF 判据，不保证内容已正确或必可触摸。
  first_fd 是最近一次转入 FD/FE 的首次成功读取，不是固件内部停扫描的精确时刻。
- 快照复制使用独立原子 try-lock，不等锁；繁忙时 SKIP 并记录 skipped。
  指标缓存不会持锁打印或访问硬件；沿用每次启动最多 8 次 fault/history 输出上限。
- 故障时另读取 0x125850..0x125853（厂商 HOST_CMD 原始窗口），记录读取返回值，
  不发送新的笔/充电/模式命令，不改固件或面板复位。原始读数不能自动视为命令已确认。
- 时钟读数与诊断有观察开销，会改变时序；不能把这个候选当作性能基准。
  此版本没有新增恢复策略，也没有“不停摆”的修复声明。
- 候选 `/Volumes/share/shim-boot-pong-perf32-spitrace-quiet.img` 已完成主机构建，
  59174912 字节、参数含 NUL 985 字节，仍配原 queue26；
  记录 `/Volumes/share/build/services32/`，日志 `/Volumes/share/logs/pong/services32/`。
  build_complete=true、returncode=0，生成源码含上述诊断。未上机，未另跑镜像哈希/审计
  或合成性能测试；不能把本轮链接成功当作时序正常或停摆已修复。

## 32.txt：边界读取没有大停顿，转入独立原生实现

- 初启 1 次 + 2 次恢复共 3 次固件下载。初始未产生正常报文，startup pending 的
  watchdog-retry 路径实际执行；随后有真实接触，最后 heartbeat 60 s 为 218 contact、
  3876 成功读、0 错误。机主仍报告约一分钟停摆；现有恢复并未根治。
- 第二组快照 good id=2474 到 first_fd id=2475，相隔 15.4 ms：
  read_us=4413/4394、byte_max_us=39/39。首次 FD 的另一组也是约 3947 us/39 us。
  捕获峰值最大字节间隔为 349 us，总读取为 14328 us，skipped=0。
  没有在被测正常→FD 边界看到长字节阻塞；这不是电气波形正确的证明，也不能排除
  未测的时序条件。不得据此提高频率、屏蔽中断或臆造已找到原因。
- 两次故障 HOST_CMD 窗口都为 00 00 00 00，status=0；copy completion 为 00，
  state=a3、CRC latch=00。没有捕获到挂起的命令或 CRC 锁存故障，历史码仍未知。
- 机主选择从零重做，直接原生 GENI/GPI SPI，不采用 GPIO SPI fallback；旧驱动保留
  回退，新旧实现要隔离。新目标同时包括小米触控笔第二代，书写/压感不以蓝牙为前提。
  原生控制器及笔输入尚未完成，不能把资料研究当成新驱动已经可用。
- 后续机主提供 Oplus 服务 ELF；其笔模式的 ioctl/厂商回调/固件配置链路已静态还原，
  见 `docs/OPLUS-PEN-RE.zh.md`。快充部分仅分析，没有执行认证节点写入或修改充电参数。

## native33-r2：独立 GENI/GPI 传输试验（仅主机结果）

- 新实现位于 `drivers/PongInputNative/`，使用 XNU 分配、ArmCpu IRQ 所有权、平台
  SMMU 租约锁和通用 ARM 4 KiB 表辅助；未调用旧触摸传输、固件或恢复函数。
  两个内核构建入口均链接独立 archive；BSD 启动按 `pong_touch_native` 独占选路，
  缺少 archive 也只 SKIP，不回落旧 NT36532。旧代码仅供回退。
- 固定 SPI4/CS0，请求 9.6 MHz，分别检查 GPI ring SID `0x5b6` 与 wrapper data SID
  `0x5a3`；不新增 bypass、不重置全局 SMMU、不挪用已分配 GPI/共享 SE DMA。
  DMA 尝试后保留内存、表、bank 与 stream；失败只隔离所持有路由和 IRQ mask。
- 参考复核发现初版漏掉 SM8475 APPS EE `0x10000` 位移。`gpi.c` 的兼容数据指定
  ee_offset=0x10000，并把 `gpii->regs` 指向 `resource_start-ee_offset`；context、
  doorbell、command、scratch、IRQ 均须减此偏移。R2 已修正且增加编译期地址断言；
  初版未上机，**不要用初版 native33 做试验**。这是明确源码错误，不是停摆根因结论。
- R2 同时纳入 FWINFO 传输失败的 FAIL/隔离，增加前提失败、命令 IRQ/状态超时、
  completion pointer/code/length 不符和 SMMU fault 快照。panic hook 只读有界缓存，
  不访问硬件、不分配或等锁，不改变真正 panic 行为。
- 2026-11-12：独立 archive 窄编译及删除生成树后的全量内核/shim 编译成功；
  源码 b68654b 加未提交改动，XNU 11417.140.69 / 43a90889846e00bfb5cf1d255cdc0a701a1e05a4。
  候选 `/Volumes/share/shim-boot-liuqin-native-input33-r2-quiet.img`，59174912 字节，
  参数含 NUL 948 字节。记录 `build/services33/final`，日志 `logs/pong/services33/final-r2`。
  `build_complete=true`、`returncode=0`；archive undefined imports 无旧触摸函数。
  按机主要求未另跑镜像哈希/审计、设备读回或 benchmark。
- 原 queue26 root 不刷；保留已有电池、背光 publish=8/parent=2、GPU level5/645MHz
  和真实 Metal 配置。fastboot 检测无设备，ADB 检测超时；**未临时启动或安装**。
- `pong_touch_native=2` 仅尝试真实 GPI 命令 IRQ、trim/FWINFO DMA，不加载固件，
  不派发触摸/笔。没有输入是当前阶段的预期，不可当成已修好或运行稳定。
  下一轮需完整 `PONG INPUT NATIVE` 日志，确认真实完成记录及 trim 后三字节
  `32 65 03`。SKIP/FAIL 先解决所有权或传输，不跨过前提继续固件操作。
- 原始独立 CSOT 固件文件尚未找到；固件初始化、GPIO21 硬件报告 IRQ、触摸 delivery
  和二代笔真实压感/倾斜/按键解析留待传输证据之后。Oplus helper 仍仅静态分析。

## 33.txt：时钟前提提前退出，未进入 DMA

- 日志内核时间 2026-11-12 16:13:55，启动参数 `pong_touch_native=2`，匹配 native33-r2。
  第 268 行明确 `selected; legacy NT36532 startup excluded`，270 行为独立 GENI/GPI
  mode2；不是旧驱动又一次报点停摆。267 行 `Pong touch: missing/incompatible Pong DT;
  SKIP` 来自不匹配的 Pong Goodix 入口，不能当作新 NT36532 驱动的 DMA 结果。
- 第 334–336 行：wrapper clocks RUNNING 后，`halt did not clear a=27008 b=80000002`，
  native input SKIP。由新驱动的前提短路顺序，GPI mapping/ownership、SE4、SMMU、DMA
  均未执行；没有新的 IRQ 完成、trim 或 FWINFO 证据。系统仍继续用户态启动。
- 这是**过严的时钟状态判据**：Linux v6.17 `gcc-sm8450.c` 的 master/slave AHB
  分支 `hwcg_reg=halt_reg`、`hwcg_bit=1`，`clk-branch.c:clk_branch_wait()` 在 HWCG
  模式直接不检查 HALT。`0x80000002` 的 bit1 已置位，空闲 HALT 不可单独判失败。
  33 未记录 enable vote readback，因此也不能仅凭 bit1 反过来称时钟或 DMA 正常。
- native34 只修此项前提：检查真实 `0x62008` vote；AHB 的已知 HWCG bit1 置位可接受
  空闲 HALT；无 HWCG 的 core/core2x 继续要求 HALT 清零。未清 HWCG、未改变共享门控，
  不新增复位、fallback 或输入服务。每个分支打印 off/vote/CBCR/hwcg/polls，最终 PASS
  仅指前提符合硬件门控语义，不代表真实 SPI/DMA 已工作。编译期断言覆盖缺 vote、
  AHB 无 HWCG 的 HALT、core 无 HWCG 豁免，以及 33 的 HWCG 状态。
- 33 电池原协议继续运行，SOCraw 从 10000 到 9900、cycle96 answers1443/errors0；
  不因此解释电流极性或修改电池逻辑。白预览/FPS 与本次时钟前提不是已证实的同一问题。
- 参考摘录放 `share/tmp/services34/native-reference/clock-hwcg-evidence.txt`；
  下一轮仍只验证真实 GENI/GPI 传输，queue26、背光、电池、Metal 保持不动。
- native34 窄编译和删除生成树后的全内核/quiet shim 编译成功，returncode=0；
  `/Volumes/share/shim-boot-liuqin-native-input34-quiet.img` 为 59191296 字节，
  参数含 NUL 948 字节。记录 `build/services34/final`，日志 `logs/pong/services34/final`。
  沿用 b68654b + 未提交修改、XNU 11417.140.69 / 43a90889 与 battery26 输入，
  跳过额外镜像哈希、审计、读回和性能测试。fastboot 检测无设备，未由主机临时启动；
  **还没有 native34 设备结果**，不能将修正判据或 PASS 文案当 DMA 已通。

## native34 后续：照片后重启，暂停试机

- 机主照片能确认：四个 wrapper 分支有真实 vote/CBCR 读数，s-AHB 的 HWCG=1、
  bit31=1 状态被按参考语义接受；wrapper clocks PASS。之后最后一条原生记录为
  `GPI ownership RUNNING`。机主随后明确报告设备重启，**native34 不再作为试机推荐**。
- RUNNING 在 `idleMasters()` 内位于 `gpi.map()` 之前；照片没有 mapping 完成或第一条
  context 读取返回的记录。并发 init 也在输出。因此尚不能将重启断言为某个 GPI 地址、
  SMMU/DMA 故障或 watchdog，更不能因无 panic 字样就认为启动安全。
- 本机 `34.txt` 仅 136 行，时间匹配 native34，但截在 CPU4 PAC 日志，比照片早。
  原始日志头 head=11770、length=8192、flush=0；记录体也没有 PONG INPUT NATIVE 或
  panic。日志保存范围不足，没有用早期截断位置替代真实设备停点。
- 主机 ADB 当前无设备，无法继续读取 reset reason/pstore。先保留 Android/REC 的
  `/sys/fs/pstore`、启动重启原因和必要 dmesg（若有），不清理，不再次启动 native34。
  后续要把映射与各个 MMIO 访问的前后记录分开；单纯在访问外套有界轮询不能防止
  一次 MMIO 不返回。没有补充证据前不新增 DMA/固件操作，不抑制真实 panic。
- queue26、背光、电池、Metal 与旧 iOS16 rollback 不动。本轮仅检查已有日志，
  更新部署 HOLD 状态，没有再临时启动、刷分区或生成新的试机镜像。

## native35：无 pstore 条件下的 CPU 映射隔离

- 机主确认 `34.txt` 就是设备实际导出的日志，pstore 在本设备不可用，并可拍摄
  崩溃前屏幕。后续不再要求 pstore；改用有界屏幕检查点与现有 PONGLOG（范围不足则
  如实记录），采集说明见 `docs/NATIVE-INPUT-PHOTO.zh.md`。
- 新增 `pong_touch_native=3`：在 `clocks()/idleMasters()` 之前独立选路，只创建 GPI
  CPU 映射。PHOTO G01..G05 分别包围 range descriptor、CPU map、descriptor release
  与虚拟地址查询；成功后 PHOTO DONE/SKIP 返回正常启动。
- 本路径不读取任何 GPI 寄存器，不投 wrapper 票、不改 SE4/引脚、不配置 SMMU 或 DMA。
  不发布触摸/笔，不进入旧驱动。它不是已修好原生触摸，也不是认定 34 重启由 MMIO
  引起；只分离此前 RUNNING 没有覆盖的 CPU mapping 过程。
- native35 独立 archive 窄编译及删除生成树后的全内核/shim 编译成功，returncode=0。
  `/Volumes/share/shim-boot-liuqin-native-input35-maponly-quiet.img`，59191296 字节，
  参数含 NUL 948 字节。回执在 `build/services35/final`、`logs/pong/services35/final`；
  源码 b68654b + 未提交修改，XNU 11417.140.69 / 43a90889。未另跑镜像哈希/审计/benchmark。
  root 与 GPU/背光/电池配置不变。fastboot 检测无设备，未上机；native34 仍暂停，
  不能重复其未定位的直接寄存器检查。

## native35 进桌面反馈：下一步只读一次 context

- 机主明确反馈 native35 直接进桌面；本轮未报告 native34 那样的重启。但没有 G05/DONE
  或 35.txt，不能据此判 mapping 已完成，更不能称 MMIO、GPI/IRQ/DMA 已工作。
  mode3 自身不做原生硬件访问，root 与显示栈仍可继续启动。
- native36 新增 selector4，先保留真实 wrapper 时钟前提和 G01..G05，再仅一次读取
  APPS instance1 TX context0 `0x914000`（resource+0x14000），就是此前 ownership 第一个
  load 的目标。不扫描其他 context，不写 GPI、不分配通道/接 IRQ，不进入 SE4/SMMU/DMA。
- PHOTO R01 在实际读取前记录 PA/offset，R02 在读取和屏障返回后记录原始值。
  PASS 只代表这一条 load 已返回，不接受所有权或建立虚假完成。任何前提失败不读；
  无重复/重试/自动重启，不抑制异常。一次 MMIO 的非返回风险仍存在。
- native36 窄编译及全新生成树的 kernel/shim 编译成功，returncode=0；候选
  `/Volumes/share/shim-boot-liuqin-native-input36-oneread-quiet.img`，59207680 字节，
  参数含 NUL 948 字节。记录 `build/services36/final`、日志 `logs/pong/services36/final`，
  b68654b + 未提交修改，XNU 11417.140.69 / 43a90889；额外哈希/审计/benchmark 跳过。
  fastboot 检测无设备，尚未上机；queue26、背光、电池、Metal 和旧 rollback 不动。
  后续继续用屏幕照片/视频及可取得的 PONGLOG，不要求 pstore。

## 36.txt：CPU 映射和首次 GPI context load 已返回

- 版本时间 17:03:08、selector4 与 native36 匹配；268 行排除旧 NT36532 启动。
  wrapper clocks PASS，G05 PASS 后，357–359 行明确 R01、R02 PASS `a=0 b=914000`、
  PHOTO DONE。实际 `0x914000` load 返回全0，不是只根据“进桌面”推断。
- 机主确认进系统；日志中未见本轮 kernel panic。没有启动通道、GPI IRQ、SE4、SMMU
  或 DMA。context 的 state=0 与未分配相符，但单个状态不是完整所有权或可运行 DMA。
  本次没有复现首次 load 重启，不因此给 native34 的整段检查确定根因。
- native37 selector5 只读 APPS instance1..6 的 TX/RX/event context0 和 IRQ-type，
  最多24条，按实际 APPS mask 限定，不读 reserved0；每次 S01 标明地址/instance/kind，
  S02 标明地址和原始值。已分配/pending 状态 SKIP、保留不清；其他 GENI 引擎不触碰。
- 只读检查完成仍返回 SKIP/DONE，不写 GPI、不分配通道或接 IRQ、不进入 SE4/SMMU/DMA。
  无 retry、合成成功或自动重启；单条 MMIO 的非返回风险仍需如实保留。
- native37 窄编译及新生成树全内核/shim 均成功，returncode=0；
  `/Volumes/share/shim-boot-liuqin-native-input37-gpiscan-quiet.img` 为59207680字节，
  参数含NUL 948字节，selector5。记录 `build/services37/final`，日志 `logs/pong/services37/final`；
  b68654b+未提交修改，XNU11417.140.69/43a90889，额外镜像哈希/审计/benchmark跳过。
  fastboot检测无设备，尚未上机；queue26、背光、电池、Metal与旧rollback不动。
  采集继续用PONGLOG和屏幕照片/视频，不要求pstore。

## 37.txt：24次 GPI只读检查完成，转到共享SE前提

- 版本17:22:12、selector5匹配。24次S02均返回0；APPS1..6 TX/RX/event/IRQ摘要均为0，
  494–495行context scan PASS和PHOTO DONE。机主确认进系统；本轮未见kernel panic。
  不称独占租约、IRQ实际路由或SPI/DMA可用，也不因此确定native34的重启原因。
- native34在这段后还盲读其他已有vote的SE DMA mode。37实际vote40028bc0选择其他SE1、SE5，
  但尚无其branch/MMIO证据。核对Linux v6.17 gcc-sm8450.c的SE0..6分支表及sm8450.dtsi，
  地址为980000+n*4000，CBCR为2700c/27140/27274/273a8/274dc/27610/27744，vote bits10..16。
  这些SE分支没有AHB那样的HWCG字段，不能把有vote等同于可读整个block。
- native38 selector6只检查已有vote的非SE4引擎：先记录/读GCC CBCR，HALT或vote丢失就SKIP
  后续SE MMIO；不强开未知引擎、不清门控或改其他SE参数。前提允许时只map并读offset258一次。
  E00为branch读取，E01为map前，E02/E03为DMA-mode读取前后；共享helper同时替换旧vote-only前提。
- SE1是已有KTZ8866背光I2C，未更改其clock config、mode、pin或亮度；SE5使用者未建立，不接管。
  不重复GPI scan、不写GPI/SE、不接IRQ、不初始化SE4/SMMU/DMA/输入。仍不保证单次MMIO必返回。
- native38窄编译及新生成树内核/shim编译成功，returncode0；
  share/shim-boot-liuqin-native-input38-sharedse-quiet.img，59207680字节，参数含NUL948字节，selector6。
  回执share/build/services38/final，日志share/logs/pong/services38/final；b68654b+未提交修改，
  XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过。fastboot无设备，尚未主机boot或刷分区。
  queue26、背光、电池、Metal及旧rollback不动；尚无native38真机结果。
  参考表在share/tmp/services38/reference/shared-se-clocks.txt，采集继续PONGLOG+照片/视频。

## 38.txt：共享SE读取完成；下一阶段为SE4时钟/控制器只读信息

- selector6/17:42:22匹配。SE1、SE5 CBCR均4220；984258、994258读回mode0，375–376行PASS/DONE。
  机主反馈未重启。本轮没有改SE或引脚，也没有GPI/SE4/SMMU/DMA工作；34原因仍未证实。
- native39 selector7只检查SE4。新增clock helper先读vote/CBCR/CMD/CFG；保留兼容已投票时钟，
  不重配置未知运行所有者/不同配置或pending UPDATE。只在unvoted+HALT下配置目标TCXO/div1及bit14，
  保留未知CFG位和其他票。真实UPDATE/branch/CFG前提失败则不读SE；该helper也替代旧无条件reparent。
- Linuxv6.17验证Rcg274e4、HID5、TCXO selector0、19.2MHz/div1，integer mode/HW control编码100001。
  C01/C02标每个clock load，C03–C06标目标写/更新/vote/等待；不改其他engine参数或全局PLL。
- 前提允许后只map SE4/wrapper并读5个通用controller字段（FW/status/IF/DMA-mode/version），
  T03/T04标PA与返回值，CAPS打印完整原始值。FW是GENI firmware revision，不是Novatek firmware或trim。
  不改pin/SE模式，不发SPI，不接IRQ/SMMU/DMA，不发布触摸或笔；只读完成不endorse其firmware/protocol。
- native39窄编译及新生成树内核/shim编译成功，returncode0；
  share/shim-boot-liuqin-native-input39-se4caps-quiet.img，59207680字节，参数含NUL948字节，selector7。
  回执share/build/services39/final、日志share/logs/pong/services39/final；b68654b+未提交修改，
  XNU11417.140.69/43a90889；额外哈希/审计/benchmark跳过。fastboot无设备，尚无native39真机结果。
  没有主机boot/刷分区，queue26/背光/电池/Metal/rollback不动；继续PONGLOG+照片/视频、
  不要求pstore。参考摘录share/tmp/services39/reference/se4-clock-caps.txt。

## 39.txt：实际SE4时钟/能力读取通过，下一步只读SMMU

- selector7/18:33:49匹配。原vote40028bc0、CBCR80004220、CMD80000000、CFG100000；
  原branch确实HALT且未投票。目标配置100001、UPDATE/bit14后vote4002cbc0，clock PASS。
- T03/T04五次读均完成：GENI FW102（SPI协议字段1）、STATUS0（无active命令）、IF1、DMA1、
  WRAP30080000；381–383 CAPS/PASS/DONE。机主反馈能进。本轮未见kernel panic。
  DMA1是模式配置，不能当成传输成功；未改pin/SE模式、未发SPI或读Novatek trim/FWINFO。
- native40 selector8只对APPS SMMU做CPU map和只读：先ID0/1/2/SCR0/GFSR，合法几何和policy后
  才扫描SMR及目标5b6/5a3相关S2CR；不使用SID当index，不读写context bank或页表，最多261load。
  A01为map前，A02/A03为每个load前后。unsupported/clientPD/fault/masked/duplicate/invalid路由
  SKIP，保留故障和路由；缺匹配也仅记录观察，不伪造媒体/服务或资源。
- 无clock/SE/GPI/IRQ变化，不分配bank/ASID或DMA；该视图非事务性独占租约，写前必须另行
  新鲜验证，不能把各阶段只读PASS合并成已取得写入所有权。34重启原因仍未确定。
- native40窄编译及新生成树内核/shim编译成功，returncode0；
  share/shim-boot-liuqin-native-input40-smmuro-quiet.img，59211776字节，参数含NUL948字节，selector8。
  回执share/build/services40/final，日志share/logs/pong/services40/final；b68654b+未提交修改，
  XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过。fastboot无设备，尚无native40真机结果。
  没有主机boot/刷分区；queue26/背光/电池/Metal/rollback不动，采集继续PONGLOG+照片/视频，
  不要求pstore。

## 40.txt：没有有效目标stream映射，下一步只读bank/ASID规划

- selector8/18:55:44匹配，ID0 4c017e71/ID1 6000004b/ID2 5111，SCR0 200406、GFSR0，
  groups113/banks75/pages128/EXIDS0，118次实际读取完成。678–680行目标5b6/5a3 matches均0，
  read-only PASS/DONE。不存在已确认可复用的目标映射，不能在无映射前提下启动DMA。
- native41 selector9只读完整SMR/S2CR，保护所有有效translate引用bank；目标已出现/活动route
  无法解释则SKIP，不接管。只读SCTLR，活动bank的TTBR0/1高32观察ASID；未启用未引用的
  非S2预留bank才读FSR，非0排除、不清。末尾复查GFSR后输出两无效槽和bank/ASID候选。
- B01为map前，A02/A03继续标每次load，kind200/201/202/203为SCTLR/TTBR0高/TTBR1高/FSR，
  最多646次。SMMU PLAN明确OBSERVED ONLY NOT RESERVED，没有分配bank/ASID、lease、DMA或表/route写入。
  非事务性视图，不认为读出的未启用状态授权写入；真正programming前需独立新鲜所有权验证。
- native41窄编译及新生成树内核/shim编译成功，returncode0；
  share/shim-boot-liuqin-native-input41-planro-quiet.img，59211776字节，参数含NUL948字节，selector9。
  回执share/build/services41/final，日志share/logs/pong/services41/final；b68654b+未提交修改，
  XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过。fastboot无设备，尚无native41真机结果。
  没有主机boot/刷分区，queue26/背光/电池/Metal/rollback不动；PONGLOG+照片/视频，不要求pstore。
  34重启原因仍未知，不能把缺route归因为旧GPIO停摆或34重启。

## 41.txt：候选完成；native42首次真实映射准备

- 41完成376load，free107/activeM1banks0/eligible68，slots112/111 bank73 ASID74，PLAN NOT RESERVED/DONE。
  引用1..5/74和faulted0 FSR400排除。SCTLR非零其他位不意味着M1；Linux/UFS/通用helper地址公式一致，
  root UFS以M0 bypass使用其引用bank，不能当空闲资源。候选不是lease或写权限。
- 通用program API要求完整2*pages*stride=100000实际MMIO跨度；旧e0000虽足当前只读地址，
  编程会Unsupported拒绝。native42实际map1MiB（ID1/UFS同范围），不伪造register_bytes或降低通用检查。
- selector10新鲜验证GPI/其他SE/目标quiescence，真实连续buffer/表分配与native4K build/clean，
  共享锁内重新选仍无效slots、未启用/未引用/无FSR bank及unused ASID。已存在目标route/active格式无法
  解释则SKIP；首store前复查slots/bank/GFSR，保留QUP默认ACTLR，不套GPU32b。
- M01分配前，M02program前，W01/W02实际store前/回调返回；返回不是硬件读回。通用program仅对
  所持有bank配置TCR/TTBR/MAIR/SCTLR、本地TLB同步及真实readback/fault检查，再仅安装两exact SID routes。
  失败fault持有targets并保留资源，不全局reset、改无关route/清无关fault，不抑制panic。
- 本轮不改pin/SE模式、不接GPI IRQ/环、不发SPI/DMA或加载触摸固件/输入；映射准备不等于传输完成。
  native42窄编译和新树kernel/shim编译成功returncode0，share/shim-boot-liuqin-native-input42-map-quiet.img，
  59211776字节，selector10，参数含NUL949字节；回执share/build/services42/final，日志share/logs/pong/services42/final。
  b68654b+未提交修改，XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过。
  尚无native42真机program结果，queue26/背光/电池/Metal/rollback不动，PONGLOG+照片/视频。

## 42.txt：真实program后StateMismatch7，保护性退回；43仅加回读差异

- selector10/19:48:39匹配，真实DMA/table backing854b94000/854ba4000，锁内slot112/111 bank73/ASID74，
  ACTLR3。实际context stores/TLBIASID/TLBSYNC后，491–493行StateMismatch7，phase6/writes15/polls2/retain0。
  没有timeout，也没成功通过native4K读回；cleanup禁用/同步，driver仍retain，GPI/IRQ/DMA/输入未开始。
- 原43前的日志只显示stores和错误码，缺helper保留的actual image，不能据此猜哪一字段或绕过校验。
  native43复用selector10和同一初始化，只打印CBAR/CBA2R/TCR/TCR2/TTBR/MAIR expected/retained actual，
  SCTLR captured/disabled0/active期望以及差异定位bitmap。此bitmap不用于忽略差异。
- helper cleanup后readback_valid已清，保留image是失败前信息，不是当前启用状态；I/O失败时还可能
  不完整。保留严格validation/TLB/fault/cleanup和资源约束，未修改PongAdreno通用代码。
- native43窄编译和新树kernel/shim编译成功returncode0；share/shim-boot-liuqin-native-input43-mapreadback-quiet.img，
  59211776字节，selector10，参数含NUL949字节；回执share/build/services43/final，日志share/logs/pong/services43/final。
  b68654b+未提交修改，XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过。fastboot无设备，
  没有主机boot/刷分区，尚无native43真机结果；queue26/背光/电池/Metal/rollback不动。
  下一轮需完整READBACK行，不开GPI/DMA、不载触摸固件，采集PONGLOG+照片/视频。

## 43.txt：唯一差异为TCR2的TBI0/TBI1，44显式请求受限策略

- selector10/20:08:09匹配。StateMismatch7/phase6/writes15/polls2/retain0仍发生，READBACK定位mask8：
  仅TCR2 expected38011/actual38071、xor60。CBAR/CBA2R/TCR/TTBR0/1/MAIR0/1/reserved均一致，
  SCTLR captured0与预启用期望0一致；并非TCR/ASID/物理TTBR地址或TLB超时。
- 新驱动显式设context.ignore_top_byte=1，使预期TCR2含TBI0/TBI1、38071。通用encoder已限制
  此策略只用于无上区、untagged lower input_bits32；现有IOVA10000000及全部已接受低32位地址
  的63..56为0，策略不会扩大/改变地址集合。不改通用比较/增加mask，不改其他配置或owner/fault/cleanup。
- native44窄编译和新树kernel/shim编译成功returncode0；share/shim-boot-liuqin-native-input44-tbi-quiet.img，
  59211776字节，selector10，参数含NUL949字节；回执share/build/services44/final，日志share/logs/pong/services44/final。
  b68654b+未提交修改，XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过；fastboot无设备，
  没有主机boot/刷分区，尚无44硬件readback，不声称已修映射或SPI/DMA。
  queue26/背光/电池/Metal/rollback不动；下一轮完整PONG INPUT NATIVE结果，仍不启输入或加载固件。

## 44.txt：TCR2修正通过，唯一差异转为SCTLR ASIDPNE

- selector10/20:41:32匹配；所有config字段含TCR2 expected/actual38071一致，非SCTLR差异mask0。
  启用后captured SCTLR27、expected-active1027，xor1000（ASIDPNE bit12），StateMismatch7/phase6/
  writes16/polls2/retain0 cleanup。尚无验证成功mapping/GPI/IRQ/SPI/DMA/输入。
- FSR400是FORMAT bits10:9，不在fault mask内，不能当translation fault。早期对bank0 FSR400
  “faulted”措辞更正为保守非0资源筛选排除，不是fault诊断。本轮不改筛选或清未知fault。
- native45只显式context.shared_asid1，由通用encoder将ASIDPNE清0、expected SCTLR27。
  原共享锁内全局unused-ASID选择/active非VA64拒绝继续生效；不改比较mask、其他TBI/IOVA/寄存器/
  fault/TLB/cleanup策略，不提前开GPI/IRQ/DMA。显式策略仍需新硬件读回通过，不宣称修好。
- native45窄编译及新树kernel/shim编译成功returncode0；share/shim-boot-liuqin-native-input45-asid-quiet.img，
  59211776字节，selector10，参数含NUL949字节；回执share/build/services45/final，日志share/logs/pong/services45/final。
  b68654b+未提交修改，XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过；fastboot无设备，
  没有主机boot/刷分区，尚无45硬件readback。queue26/背光/电池/Metal/rollback不动，
  PONGLOG+照片/视频，不要求pstore，仍不开GPI/DMA/输入。

## 45.txt：实际native4K映射通过，46合并验证IRQ/环/SPI

- selector10/21:08:34匹配，449行锁内slots112/111 bank73/ASID74 ACTLR3，548–549两exact
  streams/owned context读回PASS、mapping ready。ASIDPNE显式策略使严格validation通过；
  本轮未见kernel panic，尚无GPI IRQ/环/SPI/DMA/trim/FWINFO或触摸固件/输入。
- native46 selector2合并fresh clock/GPI/shared-SE/target/mapping前提，SE4 native QUP mux与GPI events，
  真GIC attach、event/TX/RX command ack/state、paired rings，以及实际SPI page/trim/FWINFO transfer。
  保留TBI/shared-ASID低32策略/owner/fault/cleanup，不走GPIO，不合成事件/IRQ，不载触摸固件/输入。
- P01/P02为mux/event前，命令RUNNING/PASS记录真实IRQ ack和context state，X01在实际doorbells前；
  前16实际event打印原word，严格验证pointer/code/length/channel/TX/RX完成、eventIRQ/SMMU后才copy数据。
  trim最后32 65 03须来自真实响应，任何前提/传输失败隔离自有资源、retain，不强行继续loader。
- 已核对Linuxv6.17 GENI GPI mode的E18=GSI events、E1c=SE IRQ（保持0）；SPI TRE config/go/data
  位域与原始参考一致。并非以进桌面或控制器FW协议替代真实总线完成。
- native46窄编译及fresh树kernel/shim编译成功returncode0；share/shim-boot-liuqin-native-input46-transport-quiet.img，
  59228160字节，selector2，参数含NUL948字节；回执share/build/services46/final，日志share/logs/pong/services46/final。
  b68654b+未提交修改，XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过；fastboot无设备，
  没有主机boot/刷分区，尚无46实际GPI IRQ/SPI/DMA/trim结果。queue26/背光/电池/Metal/rollback不动，
  继续PONGLOG+照片/视频，不要求pstore，下一轮完整链路结果。

## native46照片：漏GPIO tile offset，native47修正确切板级地址

- 图中SE4clock PASS后GENI setup RUNNING，最后P01 pin16 a=f010000，随后panic snapshot0开始。
  没有panic正文/PC或GPI/IRQ/SPI证据；native46暂停，不重复试。没有46.txt可补充。
- 查明新驱动板级常量错误：liuqin.dts tlmm-basef000000+gpio-offset100000，主线TLMM regf100000/
  300000，已工作的PongBacklightKtz mapsf100000，pin16..19控制offset=1000*pin、alt1=qup4。
  新代码少了tile offset，正确GPIO16为f110000、GPIO19 f113000。
- native47只修有效TLMM基址/span并加编译地址/range断言、pin16..19 mux/drive读回；错误读回停止SE/GPI。
  不改背光driver/gpio6/panel gpio0，不GPIO SPI，不放宽SMMU/IRQ/传输验证；同selector2合并试验。
  是确定的源码地址错误，但缺panic正文不声称已经完全解释重启或验证实际SPI。
- 窄编译及fresh树kernel/shim编译成功returncode0；share/shim-boot-liuqin-native-input47-tlmm-quiet.img，
  59228160字节，selector2，参数含NUL948字节；回执share/build/services47/final，日志share/logs/pong/services47/final。
  b68654b+未提交修改，XNU11417.140.69/43a90889，额外哈希/审计/benchmark跳过；fastboot无设备，
  没有主机boot/刷分区，尚无47硬件结果。queue26/背光/电池/Metal/rollback不动，PONGLOG+照片/视频。
  参考share/tmp/services47/reference/tlmm-fix.txt，下一轮看正确PA、mux readback与实际GPI/transfer结果。

## 47.txt / native48：配对channel先配置再START，捕获清除前错误

- 用户进系统，47.txt:424–430正确pin PA/mux84；550 exact SMMU mapping PASS；552真IRQ277；
  554–557 event/TX ALLOCATE ACK/state1；558–559 TX START global IRQ type4、总IRQ3失败。
  还未RX ALLOCATE或SPI submit，不能把进系统当触摸/总线成功。ERROR_LOG未保存，错误码未知。
- 参考首次gpi_ch_init1937–1953和非UART restart1467–1514都先配置配对channel，再START。
  native48拆分allocation/config与START loops；保留实际ACK/state、owner/native4K/fault/event检查。
  这是确定顺序差异，不声称已证实error4根因；不加reload/retry恢复。
- fatal IRQ任何ack前只捕获首错global/general/ERROR_LOG、contexts/scratch、当前/最后命令并atomic发布；
  worker打印一次+probe12/13，ISR无日志/分配，混合fatal不计成功ACK；硬件仍顺序读取，不称同时latch。
  保持normal IRQ清除与真实计数，异常仅清saved status/log后mask、自有资源retain/quarantine。
- host mocked实际方法单测及窄编译PASS，仅软件顺序/清除前快照/失败出口证据；fresh kernel/shim成功returncode0。
  参考share/tmp/services48/reference/gpi-pair-and-error.txt；同selector2真实trim/FWINFO合并试验，
  未载触摸固件/输入，queue26/电池26/背光/Metal/GPU/rollback不动，无pstore/哈希审计/benchmark。
- 产物share/shim-boot-liuqin-native-input48-pair-quiet.img59228160B，selector2、含NUL参数948B；
  b68654b+未提交修改，XNU11417.140.69/43a90889；回执share/build/services48/final、日志share/logs/pong/services48/final。
  fastboot无设备，未主机boot/写分区；没有48实机START/IRQ/SPI/DMA/trim verdict，下一轮实际链路结果。

## 48.txt：第一次真正独立native SPI/NT36532响应通过

- 560–569 BOTH配置后TX/RX START都真ACK2/state2，IRQ5；571–590四次3/8/3/18B实际DMA/SPI，
  六条event/code2、TX/RX pointer/长度及event IRQ/SMMU检查PASS，总IRQ9。
- 580–581 trim20 00 00 32 65 03确认NT36532回应；592–594 FWINFO38 10 b6 7b读通，
  但不代表version/固件有效，仍无触摸固件初始化、报告IRQ或输入发布。不是只凭桌面/映射声明成功。
- 47 START失败在本次配对顺序下消失，真实传输门槛已过；不宣称旧停摆已修、FPS/preview/笔已改善。
  下一阶段独立firmware/parser/loader/report/input，不再逐寄存器拆轮或加入reload recovery。
- 部署及设备观察share/build/services48/final/deployment.json、share/logs/pong/services48/device-observation.json
  已记录，host receipt保持分离；queue26/hfs/电池/背光/Metal/GPU/rollback不动，仍不pstore/哈希审计/benchmark。

## native49：独立固件loader与真实contact报告合并试验

- selector11保留48已验证GPI链，新增独立bounded parser/one-attempt cascade loader与90s report samples。
  固件local copy仅data输入生成include，无旧getter/driver/loader/recovery调用；全部硬件写前检查
  16records/4active/201924B、overflow/source bounds、SRAM-only/nonoverlap，未支持形状拒绝。
- 静态liuqin vendor hw_crc2：reset69、两CRC banks、autocopy56、各active section实际写入、
  wait1fc925to0、reset清6B、hostAE00/BOOT_RDY1；不加generic BLD_CRC_EN/POR/unknown FAST_READ。
  host地址125850（event+50），wire d0是SPI write bit，不是event+d0。
- 每transfer仍要求真event/pointer/code/length/IRQ/SMMU；后验resetA0..AF、CRC done-noerror与ILM/DLM
  实际/golden匹配、FW version反码/local version和bounded dimensions，再读65B report。
  checksum/id/duplicate/coords检查，最多12 changed-contact logs与periodic counts；重复poll不算IRQ。
  无contact不伪造，invalid32或transfer失败停止，不reload/retry；未GPIO21 report IRQ/HID/pen派发。
- 主机GPI/parser/report测试、窄编译及fresh kernel/shim PASS/returncode0，不是设备loader/可用touch证明。
  产物share/shim-boot-liuqin-native-input49-fw-quiet.img59490304B、selector11/含NUL参数949B；
  b68654b+未提交修改，XNU11417.140.69/43a90889；share/build/services49/final与share/logs/pong/services49/final。
  fastboot无设备，未host boot/写分区；下一轮启动就触摸/滑动20s（日志画面亦可，不会操作UI），
  90s后49.txt看加载/CRC/reset/FWINFO/report，queue26/Metal/GPU/背光/电池/rollback不动。

## native50 合并触摸/笔派发候选：host完成，hfs同步/安装待REC连接

- 用户要求直接合并touch/pen并授权hfs，取消只做49 observation作为下一轮；没有49设备loader结果。
- selector12保留hardware gate；7b enable ACK后实际108B SPI、touch解析与pen01/FF/F0解析，真实
  x/y/4095压力/signed tilt/distance；generic touch queue+new48B pen queue，palm抑制与失效释放/停止，
  不旧transport/loader/recovery或GPIO SPI。明确轮询，无GPIO21 report IRQ claim，不伪造checksum/buttons/auth。
- UI runtime-r2标准HID10B笔report支持压力/倾斜；report backend失败则actual virtual-stylus pressure，
  明确无倾斜，保留main-screen wake/display-off策略，不Bluetooth/Pencil身份伪造/笔电池认证。
- fresh kernel/shim及UI compile/codesign PASS，native50 image59490304B/selector12/command949B；
  source b68654b+未提交修改/XNU43a90889。parser/GPI/descriptor/remapper synthetic host tests PASS，
  不证明固件/触摸/笔已工作、停摆已修或FPS/preview改善。
- ADB/Fastboot无设备，不能同步当前hfs；queue27未生成/未刷，runtime尚未安装。已准备current REC
  source→one UI edit→full-simg保留当前Data/SystemOS/Metal的guarded流程，不直接刷旧host APFS。
  share/tmp/services50/INSTALL.zh.md，明天连接后执行；不再要今晚测试，queue26与rollback不动。

## 2026-11-13 最终：按用户授权重出本地基底simg，Linux自行刷

- 用户不要保留新增记录、会重新建库，要求旧脚本；此前current-capture流程不再是本轮前提。
  用battery26 system.cdr克隆，只替换新副本UI runtime-r2，原build-pong-userdata-simg.py打包本地
  SystemOS/Data。不会保留设备自本地基底之后的修改，这是明确用户选择，不是“同步保留Data”。
- queue27-native-touch-pen.simg已生成11432519984B，逻辑32GiB；boot50/59490304B/selector12配套。
  Root回执share/build/services50/root，日志share/logs/pong/services50/root；老输入、queue26、GPU/Metal/
  背光/ADSP电池基底/rollback未变，无host刷写/boot，仍无native50 firmware/touch/pen/HID实机结果。
- Linux安装说明share/tmp/services50/LINUX-INSTALL.zh.md已改为直接-S512M flash hfs后boot50，
  无capture要求，允许数据库重建；50.txt确认硬件gate/输入/派发，不做hash/审计/readback/benchmark。

## 50.txt / native51：未发布输入的两项解释错误

- 50.txt607–620实际4段写入201924B、autocopy0/resetA0/FW19e6，bootflags04/12；R_DLMcd21b90f
  等于最后HeaderCRC而非DLM667268cf，另FWinfo18000/28800sensor被新4095limit拒绝，所以输入门槛退出。
  Touch/pen sources5515/5956 unavailable，不是HID派发拒绝，不宣称DLM实际损坏或旧停摆原因。
- Vendor liuqin 109-byte read（index+108data）、BE16 touchXY、pressure data98+slot，pen同sensor单位；
  新驱动旧12-bit/错误pressure位置同样需修，不能只是放宽坐标阈值。
- 51 exact bank readback+preboot4段SRAM全字节比对201924B（bounded256、无retry）、boot CRCdone/noerror/
  reset/version/严格sensor几何；postboot R只诊断复用scratch。BE16 parser+checksum/id/pressure/tilt保持guard。
  属于firmware payload功能校验，不是OS image audit/hash/readback。host tests/narrow PASS，freshkernel构建中。
- root27/UIr2保持，不重刷；下一轮Linux onlyboot51看实际bytechecks/input/HID，不pstore/benchmark。
- 51 full kernel/shim成功returncode0，59490304B/selector12/含NUL949B，share/shim-boot-liuqin-native-input51-sensor-quiet.img。
  回执share/build/services51/final与share/logs/pong/services51/final，b68654b+未提交修改/XNU43a90889。
  VM未设备boot/写入，无51实机有效输入结论；Linux onlyboot，不重刷/重建库。

## 51.txt / 52：FW已证明，共同报告源valid0即停，按ready取帧并保存证据

- 51 exact201924B SRAM/golden/FW/CRCflags/sensor实际PASS，7b真ACK和两queue发布；646 valid0/bad32停。
  1751/4506虚拟pen pressure/main-screen服务后来打开，user仍no touch/pen；无raw/reason不能指定错误项。
- 52 A3 normal-run gate + GPIO21 input/raw rising latch，IRQ-enable保持0、不GIC/PDC抢route，不ArmCpu改动；
  onlyowned pin/leaf W1C/readback guards，actualGPIO ready/rise采样；idle诊断不算bad/不派发，77启动包非input。
  boundedraw108B/checksum/reason/GPIO snapshots、normal-timeout raw，32bad READY停源，无校验跳过/假ready/reload。
- host真实method setup/state/parser回归+narrow PASS，full kernel/shim中；root27/UIr2保持，不重刷。
  raw latch/真实input仍待52设备日志；virtualpen当前无tilt，FWproof不等于有效报告/旧停摆已修。
- 52 full kernel/shim已成功returncode0，59490304B/selector12/949B参数，source b68654b+未提交/XNU43a90889；
  share/shim-boot-liuqin-native-input52-ready-quiet.img，share/build/services52/final与share/logs/pong/services52/final。
  VM未设备boot/写入，暂无52实机input；onlyboot无需hfs，保留当前库。

## 53准备：52重复raw帧，GPIO ACK0/1 ABI修正

- Downloads/52.txt637–649 FW/preboot/A3/7bACK通过；651–81016份raw108B相同，checksum0c/00 reason3，
  status1/io0，offset96为resetA3记录；8110valid/32bad停源。HID2069/2085已开，未真实input。
- Linux pinctrl-msm ack按intr_ack_high选0/1，SM8450默认false→ACK0。52误写1且mock任意write清零，
  既未清配置时的raw latch，也未证明新edge。53修初始/逐pending ACK0和before/after日志、mock真实语义。
- 按vendor report循环，一次选event页后直接110B全双工/RX+2，通用address操作使page-state失效。
  idle诊断不dispatch/不算bad，所有FW/A3/checksum/id/pen/ownership门槛保持；不GPIO SPI或reload。
- host实际loop60s模拟idle+2edge仅两帧/DOWN+UP、no early stop，narrow/full kernel+shim PASS/returncode0。
  仍无53实机touch/pen/HID，root27/UIr2/Metal/背光/电池不改，onlyboot后53.txt。
- 53image share/shim-boot-liuqin-native-input53-ack-quiet.img59490304B/selector12/参数949B；
  回执share/build/services53/final与share/logs/pong/services53/final，b68654b+未提交/XNU43a90889。
  VM未boot/写设备，用户Linux只boot，现有库/hfs无需改变。

## native53触摸通过；native54-r2笔编码与只保留笔日志

- 53.txt末次2186valid/0bad、真实ACK0/DOWN-MOVE-UP到BackBoard，用户确认触摸可用稳定。
  53/queue27保留回退；这不等于长期benchmark/旧触摸所有停摆原因已解释。
- 笔已有真实hover到virtual HID dispatch0，但日志前12被hover占满，没有后续tip压力，penBad7原因也未留。
  不推断全程pressure0，不把API接受当成App写字。Virtual路径当前无tilt。
- 54-r2修virtual tip-button bit0（之前总0，Apple标准TipSwitch用bit0），跟随真实sample.touch；
  原压力/倾斜/坐标门槛与触摸传输保持，独立raw-tip/hover/leave/reject/peaks/counts日志，用户态分预算。
  按最新要求关掉正常触摸raw/事件/GPIO周期计数及UI手指/mode日志，只保留笔与必要失败。
- 窄编译、HID真实编码方法测试、pen拒绝/parser、actual触摸loop/normal-gate回归与runtime-r2编译签名PASS；
  full kernel-r2/root28构建中，旧54初版不部署。Root28用旧simg packer及原queue27准备OS克隆只更新UI；
  用户已接受本地基底重装/建库，53/27、Metal/背光/ADSP电池/rollback原件保留，不capture/哈希审计。
- Full54-r2 kernel/shim成功returncode0/59490304B、selector12/949B args，root28成功11432552752B/logical32GiB。
  share/shim-boot-liuqin-native-input54-pen-r2-quiet.img、share/pong-ipados18-userdata-32g.queue28-native-pen-tip.simg；
  services54/final-r2/root和logs/pong/services54保存host回执。没有host设备刷写/boot，无54实际笔尖/应用绘图结论。
  用户Linux按share/tmp/services54/INSTALL.zh.md更新hfs/boot带r2版本，试笔后54.txt。

## 55准备：54没有真实笔样本，回到源侧证据

- 54.txt hover0/tip0/out515/id0/reject全0，FW/A3/7bACK PASS、virtual服务已开；无任何pen event，所以
  tip-bit修正尚未被实测，不能称API拒绝/所有接触pressure0。用户确认同支笔正确亮屏测试，手指正常。
- 源码核心53/54 SPI/FW/7b方法一致，ACK后GPIOstatus读回曾随关日志删除；55恢复该read不恢复touch trace。
  因果未证实，只还原53 I/O顺序。最多4个no-pen idle known-address状态snapshot，每次restore eventpage，
  不额外配置写/pen重开/固件恢复；FF前2raw和pen统计保持，rawhost不假装有效enable-bit。
- handloop/page-restore host回归与narrow PASS，fresh55构建中；root28/UI54-r2不改，onlyboot后55.txt，
  不另刷库，不新增手指日志/BT/auth/charging操作；触摸53/27回退保留。
- 55 fresh fullkernel/shim通过returncode0：share/shim-boot-liuqin-native-input55-pen-source-quiet.img59490304B，
  selector12/949B args，b68654b+未提交/XNU43a90889；services55/final及logs/pong/services55/final为host回执。
  无55实机笔/HID结果、无hostboot/刷写；Linux只boot55，试笔后约1分钟55.txt。

## 56：以实测倾斜/压感通过主屏幕stylus路径派发

- 55已出现真实533tip/624hover/peak2987与UI tipButton1/API0（最后UI420tip/0error），但用户确认完全无响应。
  API不是绘图结果；183条倾斜阈值拒包属错误策略。最新用户确认支持倾斜，取消未部署的无倾斜草稿。
- 56保留raw signed tilt，valid flag独立，不再因超过旧±60丢XY/压力；通过几何转换到checked polar SPI。
  WebKit iOS明确stylus压力0..500、Touch选项、极坐标约定和generic5-double ABI，均已接入；actual change masks
  区分开始/移动/结束。用retained main-screen Hand集合+stylus child11，线程串行和finger/pen交接，真实类型不伪造。
- Host builder/decoder/handloop测试、实际macOS IOKit合成对象参数读回、narrow/runtime-r2/fullkernel成功；
  boot56-pen-screen-quiet.img59490304B/selector12/949B，root29-native-pen-screen.simg11432528296B/logical32GiB。
  services56/final/root与logs/pong/services56留host证据，未VM设备刷写/boot。
- root29已含UIruntime-r2，按旧simg脚本和已授权本地基底重装；53/27触摸回退、Metal/电池/背光不改。
  用户随后提供56.txt并确认笔可用；系统随手写有黑框，无边记笔画线闪退，手指画线和笔操作控件正常。
  基本笔操作得到设备反馈，完整压感/倾斜方向精度仍未独立验收。Metal57保持此输入基线，
  修复前端明确的StoreAction/resolve/失败呈现缺陷并添加无边记现场日志，实机效果待测。
  手指日志保持关闭，无pstore/镜像哈希审计/benchmark。
