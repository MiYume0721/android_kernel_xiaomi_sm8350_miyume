# Venus 5.4：PSI 轮询/触发器与 zsmalloc 占用率分组移植

## 范围与基线

源码目录：`/home/abc/kernel_venus`，HEAD `78c1b7cf4`。
在现有未提交的 AOD、系统调用等改动之上增量移植，未提交、推送或刷机。
本批没有修改触摸手势、WALT、压缩算法、zram 容量或 ROM 配置。

6.6 对照树：`/home/abc/android_kernel_common_6.6`，
`e19fb465168c754e372547711af0641d1446fea0`。

证据及备份目录：
`out-venus-5.4.302/psi-zsmalloc-20260905-QEmMPd/`。
`before/` 保存本批修改前的三个核心源文件、完整工作区差异、配置及
上一版 `Image.gz`，不覆盖此前各批改动。

## PSI

- 使用 `psimon` 专用 kthread、timer、waitqueue，替代 kthread delayed worker。
  继续使用 FIFO 优先级 1。
- 保留 `poll_scheduled` 原子门控，仅在轮询窗口过期时清零；通过
  `atomic_xchg` / `smp_mb` 保证与调度热路径状态更新的顺序，减少重复即时唤醒。
- 阈值已越过、但被每窗口一次限频挡住的事件，保留为 `pending_event`；
  后续无新压力或新增压力低于阈值时仍可补报。
- 新触发器以当前累计压力和当前时间初始化，避免把历史累计量算作新增压力。
- 只有删除最短窗口触发器时才重算最小轮询周期。
- timer/waitqueue 只在 group 初始化时创建；最后一个触发器关闭时在锁内
  分离线程和删除 timer，RCU 等待及线程退出在锁外完成。
- cgroup 最终释放前补 `del_timer_sync()`。这是 5.4 对上游
  `timer_shutdown_sync()` 的适配：此时已经没有触发器或任务可重新挂 timer，
  回调只唤醒等待队列、不重新挂 timer。
- cgroup 触发器使用 kernfs 所有的等待队列，避免触发器销毁早于同步 poll
  等待队列清理；procfs 继续用私有等待队列。

保留现有 500 ms～10 s 窗口限制、权限、压力状态定义、vendor trace 和
deferrable averages。未引入 6.6 的非特权 averages-trigger、CPU full、IRQ PSI
或整套调度器记账变化。

上游依据：

- [461daba06bdc：专用监控线程](https://github.com/torvalds/linux/commit/461daba06bdc)
- [8f91efd870ea：创建/销毁竞态修复](https://github.com/torvalds/linux/commit/8f91efd870ea)
- [710ffe671e01：减少重复轮询唤醒](https://github.com/torvalds/linux/commit/710ffe671e01)
- [e6df4ead85d9：限频补报](https://github.com/torvalds/linux/commit/e6df4ead85d9)
- [e38f89af6a13：低于阈值时的待发事件](https://github.com/torvalds/linux/commit/e38f89af6a13)
- [915a087e4c47：初始窗口修复](https://github.com/torvalds/linux/commit/915a087e4c47)
- [e2a1f85bf9f5：最小周期重算优化](https://github.com/torvalds/linux/commit/e2a1f85bf9f5)
- [aff037078eca：kernfs 等待队列生命周期](https://github.com/torvalds/linux/commit/aff037078eca)
- [5457025fa8ca：group 释放时清理定时器](https://github.com/torvalds/linux/commit/5457025fa8ca)

另外检查了上游 `fadeedd7cfc5`：其修复针对 `a5b98009f16d` 后持有
`cgroup_mutex` 创建线程的路径；当前 5.4 调用方在创建触发器前已释放该锁，
没有引入该补丁的新分阶段创建接口。

## zsmalloc

从原来的空、较空、较满、满四组，改为十二组：空、十档部分占用、满。
部分占用组的公式为 `(100 * inuse / capacity) / 10 + 1`，与 6.6 一致。
特别保留空/满的独立判断，避免 1/127 这类低占用非空页误入空页组。

- `FULLNESS_BITS` 由 2 增至 4，并增加分组容量和位字段总宽度编译期检查。
- 分配优先选择占用率较高、尚未满的组。
- 整理源页从低占用率组选择，目标页从高占用率组选择，排除空和满组。
- 去掉原有只比较链表头的 inuse 排序逻辑。
- 扩充并隔离十二组计数与对象计数；同步调整初始化、释放、迁移回填路径。
- debugfs 统计改为各占用率组；锁内取统计快照，锁外格式化输出。

保留当前 per-class 锁、handle pin、页面迁移、延迟释放、最多四个基础页的
zspage，以及原 `__zs_compact()` 搬移主循环，没有整段替换 6.6 分配器。
这是更细的选页策略，不是更换压缩算法，也不是整套新 compaction 算法。
额外链表和计数会增加每个 size class 的元数据，实际内存/CPU收益取决于负载。

上游依据：

- [a40a71e8343e：移除表头排序](https://github.com/torvalds/linux/commit/a40a71e8343e)
- [4c7ac97285d8：细粒度占用率分组](https://github.com/torvalds/linux/commit/4c7ac97285d8)
- [e1807d5d27dd：逐组统计](https://github.com/torvalds/linux/commit/e1807d5d27dd)

## 验证

- 完整 `Image` / `Image.gz` / DTB 构建，原输出目录，`JOBS=2 KPM=0`。
- 与本批之前的 `.config` 比较无差异：PSI、zsmalloc 启用；KPM 关闭；
  zram 默认仍为 lz4；MGLRU 默认关闭；`CONFIG_ZSMALLOC_STAT` 默认关闭。
- 额外单独编译开启 `CONFIG_ZSMALLOC_STAT` 的 zsmalloc 对象，格式与未初始化
  告警作为错误处理；输出到证据目录，不替换正式对象或配置。
- 源码提取逻辑测试：ASan/UBSan 下四组全部通过；包括 131,840 个占用率与
  位字段案例、201 次跨组分配/释放转换、4,096 种选页列表组合，以及 PSI
  初始窗口、共享状态、待发事件、kernfs/proc 通知与轮询门控。
- PSI 实机测试程序已经以严格告警编译为 host、Android ARM64、ARM32 版本，
  11 个运行期用例尚未在新内核手机上执行。
- `git diff --check` 通过；内核补丁 checkpatch 无 error/warning，
  仍有四项续行对齐的风格提示。

逻辑测试的锁/原子/timer 是单线程替身，不能代替真实内核并发测试。
本轮没有重新连接或修改手机，没有刷机、重启、swapoff、重设或压实活动 zram。
新内核运行、LMKD、压缩数据完整性、性能、AOD/FOD 和待机功耗都仍需刷入后回归。

测试源码和说明：`tools/testing/selftests/psi/`。
构建日志：证据目录内 `build.log`、`build-final.log`。
上游补丁原文：证据目录内 `upstream/`。

## 产物

正式输出保持：

- `out-venus-5.4.302/arch/arm64/boot/Image`
- `out-venus-5.4.302/arch/arm64/boot/Image.gz`

最终内核：`5.4.302-DSL-qgki-g78c1b7cf`，
`#8 SMP PREEMPT Sat Sep 5 10:33:23 CST 2026`。

`Image.gz` SHA256：
`12502514aaa44cca3faa7a8caf55fbc02b8742c783f0c48887749fac96a33e32`。
完整尺寸、版本号及两个产物的 SHA256 见证据目录的 `ARTIFACTS.txt`。
上一版回退文件为 `before/Image.gz`，SHA256：
`186b3784a96c608aee73a7a7ef69b9cf641e137f1f7fbcaea3c87d82bb903b07`。
该文件仅保留为回退输入，本轮未执行任何恢复或刷写操作。
