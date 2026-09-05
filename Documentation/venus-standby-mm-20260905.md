# Venus 5.4：待机检查开销与 MGLRU 启用前修正

用户场景：8 GB RAM，内存偏紧，主要抱怨熄屏待机功耗高。

## 结论及证据边界

本批做的是源码级空闲检查优化，不是已定位并实测解决了待机耗电。
已按要求重启主机 ADB；最终 `adb devices -l` 为空，`lsusb` 只有 VMware
设备，没有手机，因此未获得本轮手机的 wakeup_sources、suspend 或内存压力证据。
没有修改手机、关闭服务、修改调频/电压/热保护、刷机或重启手机。

MGLRU 对内存紧张时的回收效率值得做开启对照，但不是专门的深度待机省电功能。
此次正式产物继续默认关闭 MGLRU，将待机检查优化与回收算法变化分开。
8 GB 容量本身不能证明内存回收就是熄屏耗电原因。

## 源码修改

### 1. 低碎片率节点的有界空闲退避

`mm/compaction.c`：保留主动整理和 `compaction_proactiveness=20`。

原来主动整理在无需工作时仍按 500 ms 检查；已有的 32 秒退避仅在实际整理
没有取得进展后发生。本批增加另一种受限退避：

- 节点碎片率不高于低水位，且与上次采样一致，才逐步延长间隔。
- 间隔为 500 ms → 1000 ms → 2000 ms，最多 2 秒，不无限延长。
- 碎片率变化、超过低水位、回收活动或显式请求都会重置此空闲退避。
- 按需整理仍通过现有 waitqueue 唤醒，不必等周期定时器；处理后重置退避。
- kswapd 正在回收时不额外扫描碎片率；非空闲周期仍用原 500 ms 基准。
- 保留整理无进展时原有的最长 32 秒退避，以及主动整理关闭时无限等待策略。
- 复用本轮节点分数，减少触发判断与整理前基线的重复采样。

这是本地有界调优，不是声称整个策略直接来自 6.6。代价是低碎片率节点没有
按需唤醒时，对突然发生的碎片变化可能最多等约 2 秒才进行周期检查。

重要：`kcompactd` 保留 `set_freezable()`。系统完整 suspend、线程冻结后，
它不会每 500 ms 把整机唤醒。此优化针对系统尚未冻结或保持清醒时的空闲
检查开销；不能用它解释 modem/WLAN/触摸中断或 wakelock 导致的耗电。

### 2. MGLRU 最低保护时间恢复上游默认

`mm/vmscan.c` 原来设置 `lru_gen_min_ttl = 5 * HZ`（5000 ms）。
如果开启 MGLRU，内存紧张时过长的保护窗口可能令页面无法回收，走
`lru_gen_age_node()` 中的 OOM 分支。现改为默认 0，保留用户空间显式设置
`min_ttl_ms` 的能力及原 OOM 安全处理，没有禁用 OOM killer。

此前 MGLRU 默认关闭，所以这不是已证明的原有待机耗电来源，而是开启前
需要消除的默认策略风险。对照的 Android Common 6.6 也使用默认 0。
[上游文档说明较大保护时间可能提前触发 OOM，默认值为 0。](https://docs.kernel.org/admin-guide/mm/multigen_lru.html)

### 3. 明确的构建开关

`build_venus.sh` 新增 `MGLRU=0/1`，默认 0。它一直编译 MGLRU 支持，仅控制
`CONFIG_LRU_GEN_ENABLED`。统计开关仍关闭，KPM、原输出路径和其余构建配置不变。

需要开启对照版时使用：

```sh
env -u TWRP_DEVICE_DIR JOBS=2 KPM=0 MGLRU=1 \
  OUT_DIR=/home/abc/kernel_venus/out-venus-5.4.302 ./build_venus.sh
```

上面是后续构建用法，本轮没有生成或刷入 MGLRU 默认开启的完整镜像。
不要同时上调 swappiness、强设 min_ttl、改变压缩算法和 LMKD 阈值。

## 已完成的验证

- 原目录完整构建 `Image` / `Image.gz` / DTB，退出码 0，构建日志没有告警或错误。
- 嵌入 Image 的配置与 `.config` 一致，也与 #8 产物的配置逐字一致。
- gzip 完整性、`git diff --check`、脚本语法通过；内存补丁 checkpatch
  0 errors、0 warnings。
- 从当前源码提取空闲退避函数，ASan/UBSan 下验证 12,486,024 种输入组合，
  加上退避时间序列与重置场景，共三个测试组通过。这不是内核并发/功耗实测。
- MGLRU 默认启用分支另行编译为隔离的 `vmscan-mglru-enabled.o`；确认编译
  结果的静态开关为真，`lru_gen_min_ttl` 初始值为 0。没有替换正式对象。
- 非法 `MGLRU` 参数在修改构建配置前失败退出。
- 逐文件比对保留此前 51 项 tracked-file 差异；原构建脚本另行审查，只叠加
  MGLRU 开关相关改动。此前 PSI、zsmalloc、AOD、系统调用改动继续保留。

测试用法（源码树内，主机执行）：

```sh
sh tools/testing/selftests/vm/compaction_idle_test.sh
```

## 产物与回退

正式内核 `5.4.302-DSL-qgki-g78c1b7cf`：
`#9 SMP PREEMPT Sat Sep 5 10:53:02 CST 2026`。

- `out-venus-5.4.302/arch/arm64/boot/Image`：46,336,512 bytes；
  SHA256 `99ef10e4fd4b473223448000b570a7205156ef1e4ab9a21dac76a7369fa885b9`。
- `out-venus-5.4.302/arch/arm64/boot/Image.gz`：20,941,767 bytes；
  SHA256 `9e42a5fedf5e50b55ccc9969e88ca0db8e6eb5ffe2d8ed0a549b1e321d6f22ff`。

证据与本批之前的备份：
`out-venus-5.4.302/standby-mm-20260905-8IUHAE/`。

`before/Image.gz` 是上一批 #8，SHA256：
`12502514aaa44cca3faa7a8caf55fbc02b8742c783f0c48887749fac96a33e32`。
它只是保留的回退输入，不代表 #8 已经在手机上验证过。

## 刷入后仍需验证

先核实手机实际版本、MGLRU 运行值和 `min_ttl_ms` 是否被 ROM 覆盖，再采集
wakeup_sources、suspend_stats、RPMh 睡眠计数、PSI、vmstat 和 LMKD 日志。
保持 AOD/FOD、推送和联网功能正常；待机功耗对照应在相同网络/应用条件下，
USB 拔线并排除无线 ADB 流量的干扰。没有这一步，不能声称已解决高待机耗电。
