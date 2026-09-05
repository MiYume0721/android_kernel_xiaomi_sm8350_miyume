# Venus：默认开启 MGLRU

后续用户已取消启用：当前脚本及 Venus defconfig 已恢复 MGLRU 默认关闭，
支持代码保留。关闭版构建记录位于
`out-venus-5.4.302/mglru-off-20260905-3CnAKc/`。
下文是 #10 开启版的历史记录，不代表当前默认配置；两个厂商兼容缺口仍未修复。

按用户要求，在 #9 待机检查优化版本基础上默认开启 MGLRU。
此前报告中的“默认关闭”描述针对当时产物，不再是当前构建默认值。

**状态：#10 完整构建通过，但随后组合审查发现厂商回收兼容缺口，建议先不要刷入。**

## 随后兼容审查发现（尚未修复）

1. `CONFIG_MI_RECLAIM=y`。传统 `get_scan_count()` 会在 `mi_st()` 为真时
   使用 `mi_reclaim_swappiness()`；MGLRU 的 `get_swappiness()` 直接返回
   `mem_cgroup_swappiness()`，没有接入该覆盖逻辑。`shrink_lruvec()` 开启
   MGLRU 后直接进入新回收路径并返回。因此，小米 ST 模式被使用时，原本的
   swappiness 策略不会按旧路径生效。这是策略适配缺口，不是编译错误；
   本轮未连接手机确认 ROM 是否实际调用该模式。
2. `cam_reclaim_global()` / `mi_reclaim_global()` 仍在栈上声明独立的
   `struct reclaim_state`，只初始化 `reclaimed_slab`，没有显式初始化
   `CONFIG_LRU_GEN` 新增的 `mm_walk` 指针。MGLRU 的 `set_mm_walk()` 会读取
   该指针。当前构建命令含 `-ftrivial-auto-var-init=zero`，可掩盖这一源码
   缺陷，不能据此断言 #10 必然崩溃；但入口应改为完整初始化，优先使用已有
   零初始化的 `sc.reclaim_state` 和标准安装/清理接口，不应依赖编译器兜底。

PSI 压力统计、zsmalloc 分组和有界主动整理在本次源码路径检查中未发现直接
相互覆盖：回收入口仍保留 PSI 记账，zsmalloc 保留自身迁移锁，主动整理
仍让位于 kswapd。这个检查不等于对所有内核功能和运行期并发作了无冲突证明。
本次审查没有擅自修改上述两个厂商兼容入口，也没有刷机或提交代码。

## 修改范围

- `arch/arm64/configs/venus_defconfig` 增加 `CONFIG_LRU_GEN_ENABLED=y`。
- `build_venus.sh` 的 `MGLRU` 默认值从 0 改为 1；显式 `MGLRU=0` 仍可
  生成默认关闭的对照版本，不移除 MGLRU 支持代码。
- 保留 `min_ttl_ms` 内核默认值 0、MGLRU 调试统计关闭、KPM 关闭、lz4
  默认压缩算法，以及之前的 PSI、zsmalloc、待机整理、AOD 和系统调用修改。
- 不修改 swappiness、zram 容量、LMKD、CPU/GPU 调频或手机系统配置。

本次构建特意移除了环境中的 `MGLRU`，以验证脚本的默认开启路径：

```sh
env -u TWRP_DEVICE_DIR -u MGLRU JOBS=2 KPM=0 \
  OUT_DIR=/home/abc/kernel_venus/out-venus-5.4.302 ./build_venus.sh
```

生成的 `.config` 与 #9 比较，仅有 `LRU_GEN_ENABLED n -> y`。
其他 52 项已有 tracked-file 差异逐文件比对保持不变；两个修改目标另行核对。

## 输出与回退

输出仍为：

- `out-venus-5.4.302/arch/arm64/boot/Image`
- `out-venus-5.4.302/arch/arm64/boot/Image.gz`

构建与检查证据：`out-venus-5.4.302/mglru-on-20260905-PagDYt/`。
最终产物版本、SHA256 与验证状态见该目录的 `ARTIFACTS.txt`。

#10 版本：`#10 SMP PREEMPT Sat Sep 5 10:59:40 CST 2026`。
Image.gz SHA256：
`a1eab16c04842dc6d8496bcfda845db01e13b1b0fa7634c0b6503c27e490eb3e`。
完整构建退出码为 0；嵌入配置、gzip 完整性和默认开启静态开关检查通过。
上述检查未覆盖两个厂商接口的运行期兼容性，暂不建议刷入该产物。

上一版 #9 保存在该目录的 `before/Image.gz`，SHA256：
`9e42a5fedf5e50b55ccc9969e88ca0db8e6eb5ffe2d8ed0a549b1e321d6f22ff`。
保留备份不代表 #9 已在手机上通过运行期验证。

## 刷入后确认

本轮不刷机，也不通过 ADB 修改手机上的运行时开关。刷入后需要确认：

```sh
uname -a
cat /sys/kernel/mm/lru_gen/enabled
cat /sys/kernel/mm/lru_gen/min_ttl_ms
```

`enabled` 的 bit 0 应为 1，其余位受硬件支持影响，不要求固定为 `0x0007`。
`min_ttl_ms` 应为 0；ROM 或 root 模块可能在启动后覆盖这些值，需要查实际状态。
随后对照应用切换、后台重载、PSI、LMKD、zram 换入换出和拔线待机表现。
构建默认开启不能证明实机已经开启，也不能证明已经改善待机功耗。
