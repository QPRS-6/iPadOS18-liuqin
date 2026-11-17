# 原生输入：屏幕检查点采集

机主确认本设备不能使用 pstore。后续采用现有 PONGLOG 与屏幕照片/视频，不再把
pstore 作为继续排查的前提。34 的 PONGLOG 只保留早期输出，照片里的原生阶段是
独立证据；不能根据早期截断位置推断重启停点。

## native35：只做 CPU 地址映射

`pong_touch_native=3` 只对 GPI 的 `0x900000 / 0x60000` 范围创建 XNU CPU 映射。
**不读写 GPI 寄存器，不投 wrapper 时钟，不改 SE4/引脚，不配置 SMMU，不启动 DMA，
不派发触摸或笔输入。** 旧驱动不启动，没有 GPIO SPI fallback。

| 最后出现的检查点 | 已记录到的位置 |
|---|---|
| `PHOTO G01` | 即将创建 range descriptor |
| `PHOTO G02` | descriptor 已取得，即将创建 CPU mapping |
| `PHOTO G03` | map 调用已返回，即将释放 descriptor 引用 |
| `PHOTO G04` | 即将查询 mapping 的虚拟地址 |
| `PHOTO G05 PASS` | 虚拟地址已取得；仅 CPU mapping 成功 |
| `PHOTO DONE` | 映射试验结束，返回正常启动，不进入寄存器访问 |

每一步通过有界 `PongProbeRecord` 留下当前状态；真实 panic 可打印已缓存快照，
不加硬件读取、分配或等锁。没有为了留住画面新增停机、死循环、自动重启或延迟。
一个最后出现的 BEGIN/RUNNING 只能限定“未看到该操作返回”，不能证明是它导致重启。

## 拍摄方式

- 从启动文字出现开始录视频，保留重启前最后几秒；单张照片则尽量拍完整最后一屏。
- 优先保留 `PONG INPUT NATIVE`、`PHOTO G01..G05`、`PHOTO DONE`；若出现真实 panic，
  一并拍到 panic 正文、PC/FAR/ESR 和回溯，不要只裁取一个 RUNNING 行。
- 如果进入桌面，直接反馈“能否进桌面/是否还重启”；无需故意触发崩溃。
- `35.txt` 若能按原方式导出可一并提供；若仍只有早期输出，按保存范围记录，不当作
  未执行后续阶段的证据，也不要求重复启动来强求日志。

mapping 成功**不证明** GPI MMIO 可读、通道所有权、IRQ 或 DMA；在确认此阶段结果后，
再设计下一步最小访问。native34 保持 HOLD，queue26、背光、电池、Metal 和旧回退不动。

## native35 反馈与 native36 单次读取

机主反馈 native35 直接进入桌面，说明本轮未复现 native34 的重启；尚未提供 G05/DONE
记录，因此不能仅凭桌面区分 mapping 已成功还是可选试验失败后正常返回。

native36 的 `pong_touch_native=4` 先检查真实 wrapper 时钟前提、重复 G01..G05 映射，
然后**仅一次**读取 APPS instance1 TX context0：物理 `0x914000`、映射偏移 `0x14000`。
这是 native34 ownership 检查中第一条 context load 的地址，来自 SM8475 EE 偏移和
已允许的 APPS instance mask；不是按 SID 当索引，也没有任意地址参数。

- `PHOTO R01 RUNNING`：即将进行这一条读取，显示 PA 与 offset。
- `PHOTO R02 PASS`：这一条读取已返回，显示原始 value 与 PA；**不验证通道所有权或 DMA**。
- `PHOTO DONE one-read only`：正常返回，不读取其他 context、不写 GPI、不分配通道、
  不接 IRQ，不初始化 SE4/SMMU/DMA/触摸/笔。

若前提失败则不进行读取；没有 retry 或寄存器扫描。一次 MMIO 仍可能不返回或触发
真实异常，不能以外层软件 timeout 掩盖。请保留读取前后视频/最后屏幕和任何 panic，
或正常进桌面的反馈，不人为触发崩溃。

## 36.txt 已确认与 native37 只读扫描

36.txt 的 G05、R02、DONE 均完成，实际 `0x914000` 返回 `0x00000000`，机主确认进系统。
这验证本轮 CPU mapping 与这一次 load，但不证明其他寄存器可访问、完整通道所有权
或 DMA。native34 重启仍不能仅凭这一轮归因或排除间歇性故障。

native37 selector5 在同样的 wrapper/CPU map 前提下，只读允许的 APPS instance1..6：
每个实例 TX/RX/event context0 和 IRQ-type status，最多 24 条 load。遇已分配 context
或 pending IRQ 立即 SKIP，不清状态，也不继续其他工作。

- `PHOTO S01 RUNNING`：a 是下一条读取的物理地址；b 高32位为 instance，低32位为
  kind（0=TX、1=RX、2=event、3=IRQ-type）。
- `PHOTO S02 PASS`：a 是同一物理地址，b 是实际读回值；只表示这条读取返回。
- `APPS scan instance=...`：四次读取返回且该实例未占用后的摘要。
- `PHOTO DONE GPI scan only`：只读检查完成，未启动其他 GENI 引擎、IRQ、SMMU/DMA 或输入。

不访问 reserved instance0，不写 GPI、不分配通道。每条都带前后检查点，若某条访问
仍出错，拍到最后 S01 地址和 panic/重启前屏幕；不重复 native34，也不自动恢复/掩盖故障。

## 37.txt 已确认与 native38 共享 SE 检查

37.txt 的 APPS1..6 全部24次 load 返回0，最后 context scan PASS/DONE，机主确认进系统。
这验证本轮只读 GPI 范围，但不是独占租约或 DMA；native34 后面还有共享 SE 检查，
其重启原因仍未确定。

native38 selector6 不重复 GPI scan，只检查当前已持有 vote 的其他 SE，跳过触摸 SE4。
37 的 vote `0x40028bc0` 对应其他 SE1 和 SE5；**vote 不代表 SE block 已可访问**。
先读源码明确的 GCC branch CBCR，HALT=1 或 vote 丢失时不访问该 SE，也不强行开钟。
如果前提允许，再 CPU map 并仅读一次 `SE_GENI_DMA_MODE_EN`（offset0x258）。

- `PHOTO E00 RUNNING/PASS`：GCC branch 状态读取前/后，a 是 GCC PA，b 为 engine/CBCR。
- `PHOTO E01`：该 SE CPU mapping 前；其后的 G01..G05 是同一个映射 helper，结合 E01 PA 看。
- `PHOTO E02`：即将读该 SE 的 DMA-mode，a 为确切 PA，b 为 engine。
- `PHOTO E03`：该读取返回，a 为 PA，b 为原始值。
- `PHOTO DONE shared-SE reads only`：只读检查结束，没有发送 SE/I2C/SPI 命令或改引脚，
  不启 GPI/SE4/IRQ/SMMU/DMA/输入。

SE1 是现有背光 I2C；此试验不更改它的时钟参数、模式或亮度。HALT/lost vote/configured
DMA 等前提失败均 SKIP，保留状态，不“接管”别的引擎。guard 不保证一次 MMIO 必返回，
若仍出错，保留最后 E00/E01/E02 地址与 panic/重启前屏幕，不反复试机。

## 38.txt 已确认与 native39 SE4 只读能力信息

38.txt 确认 SE1/SE5 CBCR 均 `0x4220`，`0x984258/0x994258` 的 mode 均0，检查 DONE；
机主报告未重启。它仍不证明 SPI/DMA，且未确定 native34 重启原因。

native39 selector7 转到触摸的 SE4，不重复 GPI 或其他 SE 检查：

- `PHOTO C01/C02`：时钟寄存器读前/后。kind 为0=vote、1=CBCR、2=RCG CMD、3=RCG CFG。
- 如果 SE4 已被投票且配置兼容，保留配置；不同配置、未知运行所有者或 pending UPDATE
  则 SKIP。只在 unvoted+HALT 条件下设置目标自身 TCXO/div1 RCG 并投 bit14，保留其他票与未知CFG位。
- `PHOTO C03/C04/C05/C06`：分别为目标 RCG CFG 写前、UPDATE 前、目标 vote 前及分支等待。
  只有实际分支与配置读回符合前提，才进行 SE4 访问，不强改已运行的其他引擎。
- `PHOTO T01/T02`：SE4/wrapper CPU mapping 前；之后 G01..G05 对应该映射过程。
- `PHOTO T03/T04`：五个只读 load 前/后：FW Revision `0x990068`、STATUS `0x990040`、
  IF `0x990064`、DMA-mode `0x990258`、wrapper version `0x9c0004`。
- `SE4 CAPS ...` 与 `PHOTO DONE SE4 capabilities only`：显示原始值后返回正常启动。

此处 FW 是 **GENI 控制器固件协议版本**，不是 NT36532 的 trim/FWINFO。未改引脚、SE模式
或发 SPI 命令，未接 IRQ/SMMU/DMA/触摸/笔；读取完成不等于协议/通信可用。仍请保存任何
最后 C/T 地址与真实异常，不将 clock guard 当作单次 MMIO 非返回保护。

## 39.txt 已确认与 native40 SMMU 只读观察

39.txt 确认 SE4 原先未投票/已HALT，目标配置由100000到100001，vote由40028bc0到4002cbc0，
branch前提PASS；随后FW102、STATUS0、IF1、DMA1、WRAP30080000全部返回，机主报告能进系统。
FW协议字段是SPI、IF是GPI-only，DMA1是当前配置，**不是DMA完成或Novatek通信证据**。

native40 selector8 只map APPS SMMU，不改变clock/SE/GPI/IRQ：

- `PHOTO A01`：SMMU CPU mapping前，a=15000000、b=e0000；G01..G05是mapper检查点。
- `PHOTO A02/A03`：每个实际32位MMIO读取前/后，a是PA，A03的b是原始值。
- core kind0..4为ID0、ID1、ID2、SCR0、GFSR；之后kind高32位是stream-slot index，
  低32位100为SMR、101为相关S2CR。只读SMR以及与5b6/5a3相关的S2CR，最多261次load。
- `APPS SMMU ...` 显示几何；`SMMU target candidate/SID` 显示实际有效/匹配信息，
  最后 `route observations` 和 `PHOTO DONE SMMU observations only` 返回正常启动。

unsupported geometry/client policy/global fault或masked/duplicate/invalid target route会SKIP，
不重置/清故障，不创建页表、分配bank/ASID或写路由。无有效匹配是观察结果，不冒充已映射。
该视图没有事务性锁/独占租约，其他驱动/firmware并发变化仍需写入前重新验证。未启动DMA或输入。

## 40.txt 已确认与 native41 bank/ASID 候选规划

40.txt 完成118次load：ID0=4c017e71、ID1=6000004b、ID2=5111，SCR0=200406、GFSR0，
113个stream槽、75个bank、128个global页、EXIDS0。RingSID5b6/DataSID5a3均无有效匹配。
这不是可复用映射或DMA证据；下一步先验证候选资源，不改已有路由。

native41 selector9 只读全部SMR/S2CR，保护所有有效translate引用的bank；若目标路由已出现
或活动route不可解释就SKIP，不替换。然后读SCTLR，活动bank读TTBR0/1高32位以观察ASID；
仅未启用/未引用且不是预留S2范围的bank读FSR，FSR非0不作为候选，也不清故障。

- `PHOTO B01`：CPU map前。后续沿用 `PHOTO A02/A03`，a为PA，A03的b为实际值。
- kind高32位为slot/bank index；100=SMR、101=S2CR、200=SCTLR、201=TTBR0高32、
  202=TTBR1高32、203=候选FSR；core0..4与40相同，kind5为末尾GFSR复查。
- `SMMU bank=...` 显示活动/引用/候选状态；`SMMU PLAN ... OBSERVED ONLY NOT RESERVED`
  列出两个无效槽和观察到的bank/ASID方案，**并未占用或分配这些资源**。
- `PHOTO DONE SMMU plan observations`：返回正常启动，没有clock/SE/GPI/IRQ/表/route/DMA/输入写入。

最多646次load，没有拥有者锁或事务性快照；真正programming前必须独立新鲜验证和获得
真实所有权。不把disabled/未引用的读数直接当永久lease，不为复用资源清别人的故障。

## 41.txt 候选与 native42 真实映射准备

41.txt 完成376次load，给出slot112/111、bank73/ASID74，free107、active translation bank0、
eligible68；引用bank1..5/74被排除，bank0 FSR400也排除。active0是M位统计，不等于root/UFS
没有工作：现有UFS以M0 bypass使用其引用bank，不能重用。

核对Linux、NativeUfs和通用native4K程序，CB地址均为 `(128+bank)*4K`。SCTLR其他位非0
不授权接管。另发现编程API需 `2*128*4K=1MiB` 实际MMIO跨度，原e0000会Unsupported；
native42像现有UFS一样实际map100000，不谎报adapter长度或降低通用检查。

native42 selector10是**首次真实native4K映射准备**，不再只是观察：

- 重新检查GPI无分配/IRQ、其他SE无DMA、目标SPI/GPI能力和无active命令；实际分配连续buffer/表。
- 共享SMMU锁内新鲜选择仍无效槽、未启用/未引用/无FSR故障bank及不冲突ASID；已出现目标route
  或无法解释active格式就SKIP，不替换。首次store前再验证slots/bank/global fault。
- `PHOTO M01` 为buffer/表分配前，`SMMU LEASE selected...` 为锁内检查后的选择。
- `PHOTO M02` 为真实context程序前，`PHOTO W01/W02` 为store前/回调返回；**W02不等于硬件读回通过**。
- 保留QUP默认ACTLR，不复制GPU32b。通用API在所选bank执行实际TCR/TTBR/MAIR/SCTLR和本地TLB
  同步/读回；两exact SID routes安装后再次读回并观察故障，其他bank/stream/SCR0不改。
- 成功后保留实际mapping所有权；失败隔离所持有的target routes并保留资源，不声称可回收。
  尚不接GPI IRQ/配置环、发SPI或加载Novatek固件/输入，真实panic处理不变。

此轮涉及实际映射写入，请保留完整42.txt或最后M/W及panic屏幕；不是只读或DMA已工作的声明。

## 42.txt 读回失败与 native43 字段诊断

42.txt 显示真实连续backing、锁内选slot112/111 bank73 ASID74 ACTLR3，context实际stores及
本地TLB同步后返回StateMismatch7，cleanup phase6/writes15/polls2/must-retain0。不是TLB timeout，
也不是已成功启用映射；驱动仍保留资源，GPI/IRQ/DMA/固件/输入未启动。

native43仍为selector10，操作、目标配置、严格校验与保护性cleanup不变，只补：

- `READBACK CBAR/CBA2R/TCR/TCR2/TTBR0/TTBR1/MAIR0/MAIR1` expected与helper保留的actual。
- SCTLR同时给出captured值、expected-disabled0、expected-active值，不把未启用时的0
  错当必然失败，也不忽略真正差异；driver不改变通用校验函数。
- `SMMU readback detail`的bitmask仅帮助定位非SCTLR字段差异；不是允许差异通过的mask。

helper返回前已经cleanup，保留image不是当前active-state证明；若I/O读取本身失败，它也可能
不完整。完整43.txt比单张store屏幕更重要，若真实崩溃仍拍最后M/W与panic，不重复42。

## 43.txt 已定位与 native44 显式TBI策略

43.txt 显示非SCTLR差异mask8：仅TCR2 expected38011/actual38071，差0x60（TBI0/TBI1）。
CBAR/CBA2R/TCR/TTBR0/TTBR1/MAIR0/MAIR1/reserved均一致，captured SCTLR0符合未启用期望。
严格验证仍拒绝并cleanup，尚未成功映射或启动GPI/SPI/DMA/输入。

native44仍selector10，只将新驱动context `ignore_top_byte=1`显式请求上述TBI值。
通用encoder本就仅允许lower input_bits32、untagged/no upper时使用此策略；当前IOVA10000000
和整个低32位范围无标签，忽略63..56不会改变已接受地址。没有宽地址/上区扩展、比较mask或
generic代码修改，预期TCR2现在38071，其他配置/owner/fault/TLB/cleanup完全保留。

下一轮仍提供完整44.txt，先确认实际mapping readback PASS，再接GPI/IRQ/SPI，不直接跳固件加载。

## 44.txt 已定位SCTLR ASIDPNE，native45显式shared-ASID

44.txt 的TCR2 expected/actual均38071，其余配置均一致；已尝试启用SCTLR后捕获27，对应期望1027，
只差bit12 ASIDPNE。仍被严格拒绝并cleanup，没有已验证映射或传输。

FSR400是FORMAT字段（bits10:9），不在fault mask中，不代表本次translation fault；早期
“faulted0 FSR400”表述需按此修正：当时只因保守“非0”候选过滤排除，不能称fault证据。
本轮不改变这些保守资源筛选，只修精确配置策略。

native45 selector10只显式 `context.shared_asid=1`，通用encoder把expected ASIDPNE清0、SCTLR27。
原锁内检查继续分配全局不冲突ASID，遇无法解释active格式拒绝；不增加比较mask、不扩IOVA/改TBI，
不改其他配置/fault/TLB/cleanup。下一轮完整45.txt确认映射是否PASS，再开始GPI/IRQ，不先开DMA或输入。

## 45.txt 映射通过与 native46 合并真实传输试验

45.txt 已确认锁内bank73/ASID74、slots112/111、ACTLR3，owned native4K context与两exact SID
读回PASS，最后mapping ready。还未启动GPI/IRQ/环、SPI或DMA，不是触摸可用。

native46 selector2 合并已确认的clock/owner/mapping步骤，准备SE4 native QUP pin/event模式，
注册真正GIC level IRQ、执行GPI event/TX/RX allocate/start命令与paired rings，再实际SPI读
NT36532 trim/FWINFO。不另做逐寄存器只读轮次，不加载触摸固件或发布输入。

- `PHOTO P01/P02`：原生pin mux/SE4 event准备前，不走GPIO SPI、不复位panel。
- `GPI command RUNNING/PASS`：实际命令前，以及真正IRQ ack计数与context state匹配后。
- `SPI actual submit/PHOTO X01`：真实DMA doorbell前，显示transfer序号/长度/最后TRE。
- `GPI actual event`：前16个实际事件的4个原始word；解析检查code/pointer/length/channel。
- `SPI actual transfer PASS`、`trim=...`、`FWINFO=...`：还需真实TX/RX完成、event IRQ和SMMU
  校验全部通过；trim最后三字节应32 65 03，不合则FAIL/隔离，不假装硬件成功。

试验失败保留自有buffer/表/route，停止传输并正常返回，不合成IRQ或fallback。若进入桌面，
仍需完整46.txt才能判断是否真正通信；出现重启保留最后P/command/X与panic。queue26不刷。

## native46照片：首次pin访问地址错误；native47修正TLMM tile

照片最后P01为pin16/PAf010000，接panic snapshot hook；尚无panic正文或GPI command/SPI完成。
已确认新驱动漏掉GPIO tile偏移：当前liuqin DT f000000+100000、mainline TLMMf100000和
已工作背光driver都一致。正确pin16 ctl是f110000、pin19是f113000，不是f010000。

native46 HOLD，不重复。native47有效TLMM=f100000、span300000，编译期断言地址/范围；
只在QUP4 pins16..19做native function1/mux-drive/OE策略并读回验证，否则FAIL停止后续SE/GPI。
不改gpio6背光、gpio0/panel reset或任何其他pin，仍为selector2合并IRQ/rings/SPI试验。
地址修正是明确源码错误修复；没有panic正文时不把整次重启唯一归因于它，也不声称SPI已可用。

## 47.txt：真实ALLOCATE通过，TX START global error；native48配对启动

用户进入系统。424–430行pin16..19地址f110000..f113000和mux84读回通过；550行exact
native4K mapping PASS；552行真实INTID277绑定；554–557行event与TX ALLOCATE真ACK/state1。
558–559行TX START收到IRQ type4/global error，总IRQ3，RX尚未分配，尚无SPI提交/trim/FWINFO。
type4不是ERROR_LOG错误码：旧ISR清除前未保存global/general状态与ERROR_LOG，暂不能确认错误原因。

native48遵循参考gpi_ch_init1937–1953：先allocate/configure两个channel，再独立START loop，
每一步仍需真ACK/state，不跳过失败。此顺序是确定的参考差异，但还不是已经验证的故障根因。

- `GPI paired configured`记录两个context/ring/scratch；`GPI paired setup PASS`仅表示配置完成，
  不是START/传输成功；后续两个START必须各自出现state2/真ACK。
- 第一个fatal IRQ在任何ack前捕获global/general/ERROR_LOG及TX/RX/event context、scratch、
  当前/最后command；release/acquire发布只写一次的snapshot。硬件读取仍为顺序读，不称同时锁存。
- IRQ只清除已保存的status/log并mask；fatal+command混合IRQ不记成功ACK。工作线程只打印一次
  `GPI IRQ before-clear ... ERROR_LOG=...`，保存probe12/13；IRQ不打印、不分配、不写probe。
- 主机mock实际方法测试通过配对顺序、各失败出口、清除前捕获/首错保留/fatal不计ACK；
  只证明软件控制流，不是设备IRQ或DMA证据。窄编译及fresh kernel/shim通过returncode0。

产物`/Volumes/share/shim-boot-liuqin-native-input48-pair-quiet.img`59228160B，selector2、含NUL参数948B；
回执share/build/services48/final与share/logs/pong/services48/final。fastboot未检测到设备，未host boot/刷分区；
没有48实机START/transfer verdict。图像哈希/审计/benchmark按要求跳过。

下一轮继续同selector2完整真实trim/FWINFO链，不增加reload recovery或GPIO fallback，
不载触摸固件、不发布触摸/笔，不刷queue26/hfs。若START再次失败，需48.txt中的清除前错误记录。

## 48.txt：独立GENI/GPI真实传输门槛通过

560–569行BOTH配置后TX/RX START各收到真ACK2/state2，总IRQ5；571–590行四次SPI提交
（3/8/3/18B）对应六条实际完成记录，code2、TX/RX pointer和长度匹配，event IRQ与SMMU检查PASS。
580行trim=`20 00 00 32 65 03`，581行NT36532实际响应PASS；592–594行FWINFO=`38 10 b6 7b`，
但只是传输数据，仍明确不验证固件有效、不加载触摸固件、不发布输入。

本轮47的TX START错误未重现，已通过后续真实DMA/SPI门槛；不是旧触摸停摆根因证明、
持续报告/触摸稳定性或笔可用证明。不再追加纯映射/寄存器轮次，下一步独立固件初始化和report IRQ/
输入状态机。首错hook本轮没有实机触发，只具主机顺序测试证据。queue26/hfs/Metal不动。
设备回执`/Volumes/share/logs/pong/services48/device-observation.json`，host回执保持单独记录。

固件准备已从本地保存的generated数据数组恢复245760B到
`/Volumes/share/build/services49/firmware/novatek_nt36532_m81_fw_csot.bin`，来源见同目录`.source.json`。
只提取data，不复用旧driver/loader/recovery。NVT末尾标记、version19/e6及主ILM/DLM bounds通过；
这是重建的本地字节副本，未与独立取得的原始bin比较，不是完整cascade parser/硬件CRC或load证明。
services49目前只有host固件准备，无新kernel/shim/设备写入；下一步新parser/loader和报告IRQ/输入。

## native49：独立cascade加载、硬件启动校验与90秒真实报告采样

selector11沿用48实机通过的paired GENI/GPI/native4K传输；新增独立`PongNovatekProtocol.h`与
`loadFirmware/observeReports`，不调用旧driver/getter/loader/recovery。固件仅通过本地输入生成include，
不发布binary。Parser在chip reset/write前严格验证16条记录/保留项、4 active sections/201924B、
源bounds/溢出、SRAM范围/不重叠；未支持形状/overlay拒绝，不变成未知寄存器写入。

vendor hw_crc2步骤为reset69→ILM/DLM destination/length/golden CRC→autocopy56→四段真实GPI下载→
1fc925等待0→清6B reset→hostAE00→BOOT_RDY1。不复制generic Google的BLD_CRC_EN/POR步骤。
host地址`event+50=125850`，wire d0只是写位80，不是地址offsetd0（早期map.md混淆）。
每段依赖真正GPI completion/code/pointer/length/event IRQ/SMMU，通过之后还必须resetA0..AF、
实际ILM/DLM CRC匹配/done无error、FW version/inverse及local-version匹配、有效坐标范围才进入报告。

报告最长90秒随正常boot并行：65B真实SPI样本，checksum/id/重复id/coordinate检查，最多12条
changed contact日志，每10秒samples summary。Repeated poll样本不叫fresh IRQ/frames；无接触不伪造。
32连续invalid或transport失败立即停止，没有reload/retry。**未attach GPIO21报告IRQ、未发布HID/笔。**

host GPI、container/report单测、窄编译与fresh kernel/shim成功returncode0；暂无49设备结果。
产物`/Volumes/share/shim-boot-liuqin-native-input49-fw-quiet.img`59490304B，selector11/含NUL参数949B；
回执share/build/services49/final、日志share/logs/pong/services49/final。fastboot无设备，未host boot/写分区。
下一轮启动时马上点击/滑动屏幕约20秒（日志画面也可以；不会操作UI），90秒后取49.txt；看native firmware section/auto-copy/reset/
CRC/FWINFO与report changed/count记录。queue26/hfs/背光/电池/Metal不刷，仍不pstore/哈希审计/benchmark。

## native50：用户要求合并触摸/笔，hfs获准；当前安装仍待设备连接

selector12保留新loader全部硬件gate，再7b enable真ACK、持续108B SPI（vendor109B含index）、
touch checksum/id/坐标验证、pen offset65 format01/FF/F0和实际XY/压力/distance/signed tilt。
未知格式/越界不派发，vendor pen checksum OFF不编造checksum/buttons/auth。坐标归一化后进入
generic `/dev/pong-touch`和新的48B `/dev/pong-pen`；pen在范围内抑制palm/fingers，失效释放并停源。
明确8ms sleep轮询，尚未attach GPIO21报告IRQ；不是GPIO SPI/旧loader/reload recovery或假IRQ。

配套UI runtime-r2派发标准HID笔10B report压力/倾斜；若report-device后端不存在，使用已经有的
event-system stylus pressure路径，明确没有倾斜，不猜privatefield。屏幕关闭时不派发笔/POWER。
没有Apple Pencil身份/Bluetooth/充电认证依赖。Kernel-only不能让旧queue26 UI库开始支持笔，必须配套hfs。

host kernel/shim returncode0：share/shim-boot-liuqin-native-input50-touch-pen-quiet.img59490304B、
selector12/含NUL949B；UI share/build/services50/runtime-r2/libpong-ui.dylib编译签名通过，
tests验证parser与48B/10B ABI及synthetic live-remapping。都不是设备firmware/可用input证据。

用户现已允许hfs刷写，但ADB/Fastboot无设备，当前FS尚未capture、queue27 root未生成/未刷。
不能用旧APFS host metadata直接覆盖。新增current-live-hfs builder先严格确认liuqin REC、未挂载
hfs32GiB/SHNGROOT、捕获当前系统，再begin/stage/finish只替换UI库，生成保持当前Data/SystemOS/Metal
及OS之外字节的simg，明确授权--flash才`-S512M flash hfs`/temporary boot。无目标fallback。
安装说明share/tmp/services50/INSTALL.zh.md；用户要休息，不再请求今晚测试。queue26/rollback保留。

## 2026-11-13：Linux主机采集/自行刷，VM离线生成simg

用户现在能测，但设备在Linux主机、USB直通不稳定；改成Linux REC只读capture当前hfs到share，
VM `--capture`读取完成的gzip/receipt，生成完整配套simg交用户Linux刷。不要求直通，不由VM写设备。
当前源尚未收到，不能用旧queue26/host APFS替代当前Data。collector只接受liuqin REC/unmounted hfs32GiB，
检查holders/mount及SHNGROOT窗口，无hash/审计/readback比较/benchmark，采集是当前FS更新来源。

VM data-only替换已有UI库，通过自己新建OS副本的`-owners off`保留已有inode owner/mode，
不chown、不改宿主安全设置、不需要sudo密码。合成APFS夹具uid501挂载/写入/元数据保持测试通过；
真实当前OS仍待收到后处理。capture mode禁止--flash，用户Linux自行刷。
最新说明share/tmp/services50/LINUX-INSTALL.zh.md；Linux共享目录运行
`python3 tmp/services50/capture-current-hfs-linux.py --out native50-current-hfs.raw.gz`，完成后保持REC，
提供gzip和对应json，再出simg。暂未生成queue27/刷hfs，native50触摸/笔实机效果未验证。

## 最终选择：恢复本地基底，用旧脚本生成queue27（不采集hfs）

用户明确要求“用之前的脚本”和“不用保存记录，我重新建库”，允许不保留设备这几轮新增
数据库/记录。已复用battery26的cp-c clone/ownersoff挂载与原build-pong-userdata-simg.py；
只在新OS克隆副本更新UI runtime-r2，本地SystemOS/Data原样打包，不称保留当前设备Data。
queue26、原始输入/Metal/背光/ADSP电池基底及rollback未改，不再要求capture/直通/sudo。

配套`/Volumes/share/pong-ipados18-userdata-32g.queue27-native-touch-pen.simg`现已生成11432519984B，
逻辑32GiB；boot50为59490304B/selector12，UI运行时已包含。Root回执share/build/services50/root，
日志share/logs/pong/services50/root/simg.log，用户Linux自行-S512M flash hfs然后temporary boot。
尚无native50固件/触摸/笔/HID实机结果，初次建库可能较慢，测后50.txt判断，不用pstore/哈希审计/benchmark。

## 50.txt 无触摸：后验门槛解释错误，native51只换kernel

607–614四段201924B实际native writes/auto-copy0，615 resetA0、616 boot flags04/12，
617 FW19/e6、38/60 channels、18000/28800 sensor。但R_DLM=cd21b90f恰为最后Header15 CRC，
不是primary DLM667268cf；新驱动又用4095/12-bit限制拒绝sensor值。620门槛失败先于任何
report/pen-enable/node，5515/5956 BackBoard只能等不到源。不是UI派发失败或已证实DLM损坏。

vendor workfunc6bbc/6bc0、6bdc使用BE16手指XY，6c10的pressure来自point99+slot（去index后data98+slot），
不是旧12-bit格式/width byte；触摸与笔都要用真实sensor单位归一化，不简单放宽旧错误解析。

native51加强完整性而非跳过CRC：CRCbank exact读回，实际copy0后在MCU启动前按256B块验证所有
active SRAM payload共201924B，任一byte/I/O失败立即停止不reload；再bootCRC flags/reset/version和严格
18000/28800/channels38/60。Postboot RCRC只作cascade复用scratch诊断。报告改108B/BE16/tailpressure。
host回归和真实verifier方法mock测试、窄编译PASS，fresh build中；暂无51实机byte/report/HID结果。
rootqueue27/runtime-r2/pen48B ABI不变，不重刷hfs，用户Linux只boot新kernel、收51.txt。

51 fresh kernel/shim已成功returncode0，share/shim-boot-liuqin-native-input51-sensor-quiet.img59490304B、
selector12/含NUL参数949B；回执share/build/services51/final、日志share/logs/pong/services51/final。
VM未boot/刷分区，暂无51实机结果。Linux共享目录`fastboot boot shim-boot-liuqin-native-input51-sensor-quiet.img`，
无需重建库/重刷queue27；看新preboot byte/golden、FW sensor gate、actual touch/pen/HID，或首次真实失败。

## 51.txt：固件已完整证明，报告源过早停；native52正常状态/ready门槛

605–642 exact banks/all201924B SRAM/auto-copy/reset/FW19e6/sensor/bootflags全部实际PASS；643–645
pen-enable真ACK、touch/pen queue成功。646有效0/拒绝32停共同源，1751虚拟笔pressure服务、4506触摸服务
后来已打开。用户仍无touch/pen。没存被拒原始帧/具体reason，不能声称是checksum/坐标哪一项，更不是HID拒绝。

52保留完整FW证明，先确认normal-runA3（51只见A0 init），GPIO21 input/raw-rising latch设置并exact读回；
IRQ-enable bit0保持0，不打开未拥有GIC240/PDC132，不改ArmCpu/其他pin/target/wake策略，已enabled owner拒绝。
基于真实GPIO21 latch/rise才读取报告，只有自有leaf W1C；raw是polling不是report IRQ计数。
最多8次idle诊断不派发/不算bad；first16 raw保存108B+reason/checksum/GPIO，A3 timeout也raw一次。
vendor77启动包不当触摸/坏帧；checksum/id/coords/pen规则仍强制，32bad READY才释放停源、不reload/retry。
只是已确认初始化/取帧流程差异的合并修正，具体拒绝原因仍待原始数据，不能声明已修好。

host readiness/parser/verifier tests和窄编译PASS，fresh52构建中。Root27/UIr2无需重刷，无52设备结果；
当前virtual pen仅压力无倾斜，真实性需report/HID证据，不pstore/哈希审计/benchmark。

52 full kernel/shim成功returncode0：share/shim-boot-liuqin-native-input52-ready-quiet.img59490304B，
selector12/含NUL949B；回执share/build/services52/final、日志share/logs/pong/services52/final。
暂无52设备ready/frame/HID结果，VM未boot/写设备；Linux only `fastboot boot shim-boot-liuqin-native-input52-ready-quiet.img`，
现有queue27/UIr2不动，测试后52.txt看GPIO raw/normal-state/拒绝原因或真实input，不重刷hfs。

## 52.txt：明确checksum失败与GPIO确认值错误，native53 ACK0/direct burst

输入`/Users/qprs/Downloads/52.txt`保留原样。637–649固件完整性/版本、A3正常状态和笔ACK均通过；
642–644 GPIO21 raw-enable后status1/io0，651–810前16份完整108B完全相同，checksum计算0c/收到00，
reason3，offset96包含A3 reset记录。811有效0/拒绝32停源；2069/2085笔/触摸HID后来打开。

查明本次源码寄存器ABI错误：Linux `pinctrl-msm.c::msm_ack_intr_status` 根据intr_ack_high，
SM8450未设此字段，所以GPIO21必须写**0**确认。52照搬GPI W1C写1，未证明raw-enable可能产生的
pending已清除，不能把32次status1记作32个新硬件边沿。旧hostmock又把任何write都清零，掩盖了错误。

53修ACK0、初始化/每次pending的前后读回；保留IRQ-enable0和原owner/config/firmware/A3/checksum门槛。
按vendor work_func改为命令结束后选事件页一次、随后直接110B SPI read/RX+2取108B；通用地址访问
使事件页状态失效，避免暗用错误页。Idle diagnostics仍不派发/不计bad。真实新报告是否正常还待设备。
修正mock并跑真实runInput方法的60s合成idle/两个外部edge：只两份ready→DOWN/UP、ACK0清除、无早停；
窄编译与fresh53 kernel/shim成功returncode0。仅boot更新，queue27/UIr2不刷；无53可用触摸/笔结论。

产物share/shim-boot-liuqin-native-input53-ack-quiet.img59490304B、selector12/含NUL949B；
回执share/build/services53/final与share/logs/pong/services53/final。Linux共享目录only
`fastboot boot shim-boot-liuqin-native-input53-ack-quiet.img`，进系统后真实触摸/笔并取53.txt。
VM未写/boot设备，无53实机结果；不重刷hfs或重建库。

## 53实机触摸可用稳定；54-r2专注笔及笔日志

用户确认“触摸能用，稳，笔不能用”。Downloads/53.txt ACK0真实1→0、DOWN/MOVE/UP到BackBoard，
末次2186valid/0bad；作为本次实测稳定，不扩大为无限时稳定。Native53/queue27保留触摸回退。
笔7bACK、真实hover位置/tilt到虚拟压力服务dispatch0；前12日志额度被hover用完，后续tip压力未知，
7个penBad原因未知，不能断言所有压力为0。当前virtual HID无倾斜派发，也不能把API接受当作App写字成功。

查明virtual send_pen把button_mask固定0，Apple标准TipSwitch映射bit0；54-r2按真实sample.touch设置
这个位，hover不伪造落笔。内核仅增pen详细拒绝分类/rawpressure/峰值/hover-tip-out-ID计数与独立日志预算，
不改pressure/tilt/XY门槛。用户态同样分开tip/transition/dispatch日志，第一批hover不能再盖掉tip证据。

按用户新要求关闭正常手指事件、完整触摸raw、每帧GPIO与周期触摸计数、BackBoard手指/mode日志，
保留笔日志和必要启动失败。传输/固件/ACK0/手指功能保持native53，trace-suppression后的实际loop回归通过。
之前已编好的54初版被未部署的54-r2替代；matching runtime-r2已编译签名，kernel-r2/root28构建中。
root28按用户选择复用原打包脚本/本地queue27 OS克隆，只更新UI库；不保留设备新增库的承诺不变，
53/27和原输入不覆盖。没有54-r2实机tip/绘图结论，需下一轮原始tip/拒绝原因/API/App证据。

54-r2 full kernel/shim returncode0和root28现已完成：boot54-pen-r2-quiet.img59490304B，
queue28-native-pen-tip.simg11432552752B/logical32GiB，selector12/949B参数；UIr2已含在root28中。
回执share/build/services54/final-r2、services54/root与logs/pong/services54；未host刷写/boot，无54实机结论。
Linux安装说明share/tmp/services54/INSTALL.zh.md：-S512M刷新hfs后boot带r2的新kernel，再试笔轻重划线/抬笔，
取54.txt。正常触摸日志已关闭，仅笔证据和必要启动失败；沿此前选择本地基底重新建库，不要求capture。

## 54.txt：没有进入HID的笔包；55保持手指路径做源侧核对

这次hover0/tip0/reject0/out515，raw/accepted峰值0，固件/A3/7bACK已过，virtual服务已开但无任何pen事件。
用户确认手指正常，同一支二代笔已离开磁吸边、在亮屏悬浮与落笔。不能归咎漏测，也不能拿这轮
评价tip-button修正被iPadOS拒绝：该代码没有收到真实接触。当前证据停在pen report来源之前。

核对53/54-r2：SPI/read/page/firmware/pen-enable核心方法相同，关触摸日志时删掉了ACK0后的status读回。
55恢复此硬件读回但不恢复触摸日志，建立53的I/O顺序；不能声称这个差异就是笔丢失的已证实原因。
尚未收到hover/tip时最多4次idle只读已知host/reset/FWINFO+GPIO状态，必定恢复report页再继续；
rawhost数据不冒称有效pen-enable位，不额外7b重开/固件reload/蓝牙/充电认证写。前2个FF包也留笔raw。
host实际hand-loop/page-restore测试和窄编译PASS，full55构建中，暂无55实机效果。
root28/UI54-r2不变，不重刷hfs；保持已验证53/27手指回退，后续看55.txt源状态/pen packet/totals。

55 full kernel/shim已成功returncode0，share/shim-boot-liuqin-native-input55-pen-source-quiet.img59490304B，
selector12/含NUL949B。回执share/build/services55/final与share/logs/pong/services55/final。
没有VM设备写入/boot，没有55实机笔结果；Linux只boot55，进入系统试笔后约1分钟取55.txt。
已刷queue28不再重刷/重建库，正常触摸日志继续关闭。

## 55真实落笔/压力已到API；56改主屏幕stylus集合并保留倾斜

55.txt7466起真实pressure814→1278及tipButton1/API0；最终kernel533tip/624hover/peak2987，
UI末次打印420tip/0error，183包只因tilt61+被整包拒绝。用户明确屏幕仍完全无反应，因此不能把
API0当成BackBoard/UIKit成功。用户先误认为不支持倾斜，后明确确认支持；不含倾斜的host草稿未部署。

56保留有符号实测倾斜，±90物理平面角有效性单独标记在原48B ABI的reserved位，异常角只省略方向，
不丢XY/pressure。用户态几何转换为极坐标，使用WebKit iOS核对的专用polar stylus接口、Touch选项0x20000，
压力从真实0..4095转为iOS stylus0..500。事件mask只标记实际状态变化，移动不反复冒充新落笔。
Generic接口纠正为5个double/32位boolean_t，移除旧多余twist参数。无估算/编造角度或压感。

Routing使用已经工作的主屏幕digitizer：Hand集合+真实Stylus type0/index11子事件；主屏幕引用与派发
串行保护，笔进范围先release旧finger，旧UP不会清掉新pen集合，pen线程不移除借用服务。不是把笔伪装成
手指或Apple Pencil，不依赖Bluetooth。触摸normal日志保持关闭，显示关断/背光/真实GPU/电池链保持。

Host mock验证事件层级/压力/Touch/移动mask和角度转换，另macOS真实IOKit合成对象读回type0/index11/
touch1/pressure250/altpi4/azpi2通过；这不是iPadOS绘图证明。Narrow/runtime-r2/freshfullkernel成功，
boot56-pen-screen-quiet.img59490304B、selector12/949B，root29-native-pen-screen.simg11432528296B。
Root29用用户选择的旧脚本/本地基底只换UI库，53/27回退与旧输入保留，无host刷写/boot。

用户随后提供56.txt并确认“笔可以用了”。新问题是系统随手写黑框、无边记仅笔画线闪退；
手指画线、笔切换笔刷和打开文件均不闪退。这是基本笔可用的设备反馈，尚非独立的压感曲线/
倾斜轴精度验收。Metal57沿用native56与runtime-r2，相关图形修正及待验证项见PongMetal README。
