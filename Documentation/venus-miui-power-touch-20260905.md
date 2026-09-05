# Venus 5.4：小米轻任务选择、core_ctl 策略、触摸状态去重

## 状态与范围

2026-09-05，在 `78c1b7cf4e64e93a46f586c187b0374487c53f17` 的现有混合工作树上增量移植。
保留已有 PSI、zsmalloc、VFS、AOD 单击等修改。没有提交、推送、刷机或连接 ADB。
构建脚本和 venus_defconfig 本轮未修改；KPM 关闭，MGLRU 编译但默认关闭。

参考源码：

- MiCode/Xiaomi_Kernel_OpenSource，`popsicle-w-oss`，
  `45705be1220b4cfa8100516ad86711656c0b634e`。
  `kernel/sched/walt/walt.h` 的 `MIUI ADD: Task_Attribute_Sched`、
  `core_ctl.c`、`sysctl.c`；该树 ACK_SHA 指向 Android 16 / 6.12。
- MiCode/vendor_xiaomi_proprietary_touch-driver，`popsicle-w-oss`，
  `c286ab85f4982c9b5967e18405f4e2da0332ce4d`。
  `xiaomi/xiaomi_touch_core.c` 的休眠／恢复工作函数状态检查。

## 1. 小米轻任务 CPU 选择

落点：`kernel/sched/fair.c:walt_miui_packing_cpu()`。

- 参考小米在聚合路径中比较前两个可用 CPU、避免聚合较重任务的逻辑。
- 保留 donor 的轻任务门限：task util < 15，候选 CPU util < 30；
  这些是调度器 utilization 数值，不是 15% / 30%。簇利用率门限为 40%，
  频率比例上限为 450/1024。
- 5.4 没有 donor 的簇利用率缓存，改为汇总当前 active、未隔离 CPU 的
  utilization/capacity；这与 donor 的上一窗口缓存不是完全相同的采样方法。
- 仅在原 WALT 已选出非空目标且通过既有 packing 检查后，对起始簇中的
  前两个合格 CPU 进行选择。不新增跨簇迁移、不用于最高性能簇。
- 排除 active migration、RTG、boost、需要 idle/低延迟的任务；保留
  affinity、CPU isolation、reserved CPU、高 IRQ 负载、容量和深 idle 限制。
- 保留原 idle 候选和 EAS 能量比较，不将该策略变成无条件返回 CPU 的快路径。
- 未移植 donor 中仅有定义、没有消费路径的 MIUI TRAILBLAZER/IPC 开关位。

这是面向 5.4 的保守适配，非整套新 WALT 替换，尚无实机性能／功耗收益证明。

## 2. core_ctl 可恢复阈值策略

落点：`kernel/sched/walt/core_ctl.c`、`kernel/sched/walt/miui_power.h`。

- Xiaomi donor 对所有簇直接写 70/40，关闭时直接写 60/30；本移植不覆盖
  `busy_up_thres` / `busy_down_thres` 的 ROM 基线。
- 策略启用时，每个簇、每个 active-count 档位在其基线上同时增加至多 10
  个百分点；up 不超过 100，原上下阈值差保持不变。60/30 会得到 70/40。
- 任一阈值为 0、up >= 100 或 down >= up 时保留原值，避免改变特殊配置语义。
- 基线 sysfs 写入和策略计算使用 state_lock；sysctl 更新先解析到临时变量，
  拒绝非法位后才发布，并在 bit 3 切换时触发 core_ctl 重新评估。
- ROM 在策略开启期间改写基线仍然有效；关闭策略直接使用最新基线，无陈旧备份覆盖。
- 不改变 min_cpus/max_cpus、boost、热控和 offline delay。

新增只读节点（各簇现有 core_ctl 目录下）：

- `busy_up_thres_effective`
- `busy_down_thres_effective`

原 `busy_up_thres` / `busy_down_thres` 继续表示可写基线，新增节点显示实际判定阈值。

## 3. 触摸休眠／恢复去重

落点：`drivers/input/touchscreen/fts_spi/fts.c`、`fts.h`。

- event_wq 改为 ordered workqueue，串行执行状态切换。
- 两个工作函数内检查 `power_state_valid` 与 `sensor_sleep`，只有上次状态切换
  成功且目标状态相同才跳过。首次状态未知时不跳过。
- 聚合 reset、mode、IRQ、factory regulator 操作结果；失败不标记 valid，允许重试。
- 显示通知和 Touch_Power_Status 不再仅凭旧 sensor_sleep 在入口丢弃请求。
  先排空旧工作，再交由工作函数判断，覆盖 OFF 已排队但 ON 随即到达的情况。
- AOD、双击、non-UI 的 `switch_mode_work` 保留独立更新路径；LP1/LP2 自动补回
  AOD 单击的已有修复未删除。指纹按压期间的免 reset 逻辑保留。
- 不删除或禁用单击 AOD、双击亮屏、FOD、掌纹或触摸 HAL 接口。

## 开关与默认状态

`/proc/sys/kernel/sys_miui_power_enhance`：

| 值 | 功能 |
|---|---|
| 0 | 两项调度策略关闭，默认值，与 donor 一致 |
| 4 | 仅轻任务 CPU 选择 |
| 8 | 仅 core_ctl 阈值策略 |
| 12 | 两项均开启 |

其他位会被拒绝。触摸去重无需开关，刷入此构建后生效。
本轮没有在手机上写入任何值。源码具备策略不等于量产 ROM 已调用，也不等于
刷入后策略自动开启；后续应分别以 0、4、8、12 做对照，最终默认值由实机数据决定。

## 验证

主机自测：

```sh
bash tools/testing/selftests/venus/run_host_tests.sh
```

测试从当前源码提取实际 C 函数，与主机替身一起编译，启用 UBSan。
不是重新实现一份算法后测试，也不写主机／手机的 sysctl 或 sysfs。

- packing：4+3+1 拓扑、前两个候选、空 mask、affinity/isolation、负载／频率／
  容量／延迟保护。
- core_ctl：合法位、非法位／解析失败不改变状态、读操作无副作用、重复切换、
  启用中 ROM 基线变化，以及 10,609 组阈值组合和 UINT_MAX。
- touch：普通／factory 两种预处理配置；重复休眠／恢复、reset/mode 失败重试、
  排队 OFF/ON 顺序、LP1/LP2、单击 AOD、双击、non-UI、FOD。

四个测试程序全部通过。硬件、真实 IRQ/锁并发和 ROM 消费端使用替身，不能由此
宣称实机手势正常、长期省电或调度无回归。

构建：

```sh
env -u TWRP_DEVICE_DIR JOBS=2 KPM=0 MGLRU=0 \
  OUT_DIR=/home/abc/kernel_venus/out-venus-5.4.302 ./build_venus.sh
```

完整 Image / Image.gz / dtbs 构建退出码 0；gzip 完整性、嵌入配置与输出配置匹配、
以及输出配置与上一版配置完全一致的检查均通过。`git diff --check` 通过。
调度补丁 checkpatch 除 `return -1`（无候选 CPU 哨兵值，不是 errno）规则误报外通过。

## 产物、备份和后续验收

- 版本：`#12 SMP PREEMPT Sat Sep 5 12:12:52 CST 2026`。
- 新镜像：`out-venus-5.4.302/arch/arm64/boot/Image.gz`。
- Image.gz SHA256：`321c7a7e6386d2e8c3ed061c4790b99fb419d76fa855ca2ab37dfba2dbea750c`。
- Image SHA256：`323c48918f0c5cd615b21ed0c32f479c7dcdc6559c5b69c3236f83afc3a9a3ba`。
- 构建日志、旧镜像、旧配置和测试程序：
  `out-venus-5.4.302/miui-port-20260905.ppoqmI/`。
- 该目录中的 Image/Image.gz 是上一版 #11，不是本轮新产物；旧 Image.gz SHA256：
  `bbe79c9f50e959f023eb65f6e020181fe89e8b6209adb2805a7c1c3a5e9cbaf4`。
- 改动前源文件临时备份：`/tmp/venus-miui-port-baseline.C6uuGG/`，可能随主机重启清理。

尚未刷入。后续验收需确认实际内核版本／开关，测试单击 AOD、双击、FOD、快速
熄亮屏与失败恢复；检查帧时间、任务迁移、各簇频率/idle/core_ctl，以及拔线待机。
调度仅优化 CPU 醒着执行任务时的选择，不能代替唤醒源排查或保证降低待机功耗。
