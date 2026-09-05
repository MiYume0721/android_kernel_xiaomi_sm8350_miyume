# Venus 5.4：VFS / pidfd 回移批次（2026-09-05）

## 范围与基线

- 目标树：`/home/abc/kernel_venus`，分支 `venus-5.10bpf`，HEAD `78c1b7cf4`。
- 参考树：`/home/abc/android_kernel_common_6.6`，Android 15 / 6.6，提交 `e19fb465168c754e372547711af0641d1446fea0`。
- 保留此前未提交的 AOD 单击修复、MADV_POPULATE、epoll_pwait2、futex_waitv、MGLRU 配置及其他已有改动。
- 这些是 6.6 已具备而当前 5.4 缺少的能力，不能都称为“6.6 首次引入”。本批不替换调度器、内存回收核心或触摸驱动。

## 已实现

| 接口 | ARM64 / AArch32 compat 调用号 | 内容 |
| --- | --- | --- |
| openat2 | 437 | 24 字节 open_how、参数和大小校验、五种路径解析限制 |
| pidfd_getfd | 438 | 有权限地复制目标进程文件描述符，保留共享文件偏移，返回 CLOEXEC fd |
| faccessat2 | 439 | AT_EACCESS、AT_SYMLINK_NOFOLLOW、AT_EMPTY_PATH |

`openat2` 支持 `RESOLVE_NO_XDEV`、`RESOLVE_NO_MAGICLINKS`、`RESOLVE_NO_SYMLINKS`、`RESOLVE_BENEATH`、`RESOLVE_IN_ROOT`。**不支持 RESOLVE_CACHED**；该位作为未知标志返回 `EINVAL`，没有宣称实现仅缓存解析。

额外性能优化：在 real/fs UID、GID 和能力集无需变化时，`access/faccessat/faccessat2` 跳过临时凭据分配和引用计数操作；凭据不等价时仍走原有覆盖路径。这是减少特定系统调用开销，尚无手机性能、帧率或功耗收益测量。

## 安全与 5.4 适配

- `open_how.flags` 在 64 位宽度下校验，拒绝高位未知标志；检查最小结构大小、超过 PAGE_SIZE 的大小、扩展尾部和非法 mode。
- BENEATH 与 IN_ROOT 互斥；根目录固定、重命名/挂载序列号和最终路径归属检查一并回移，遇到不确定的并发 `..` 解析可返回 EAGAIN。
- `nd_jump_link()` 改为传递错误；同步调整 proc 普通链接、namespace 链接和 AppArmor 调用者，适配 5.4 的指针型 `ns_get_path()` 返回值。
- NO_XDEV 同时覆盖 RCU、引用计数路径、自动挂载及 `..` 后向下穿越挂载点；非受限的原有 mountpoint 调用仍传 flags=0。
- `O_DIRECTORY | O_CREAT` 按较新上游语义返回 EINVAL，避免调用失败却创建普通文件。旧 open/openat 对多余 O_PATH 标志的忽略规则保留。
- 现有 SuSFS 隐藏/重定向逻辑保留；重定向再次调用路径解析时继续使用同一组 resolve 限制，不绕过边界检查。
- `pidfd_getfd` 保留 `PTRACE_MODE_ATTACH_REALCREDS` 和 `security_file_receive()`，不放宽 SELinux 或 ptrace 权限；接收 socket 时补 netprio/classid 更新。
- 使用 5.4 适配的 `exec_update_mutex`，不使用覆盖用户态等待的 `cred_guard_mutex` 作为 pidfd 取 fd 的同步锁；锁覆盖 mm/凭据转换并包含成功与错误释放路径。
- exec 同步锁在非调试配置下使用 signal_struct 现有 4 个 64 位 KABI 保留槽，并通过尺寸/对齐静态断言；大型调试锁配置使用独立字段，不宣称保持生产 ABI。
- 复用此树已有的加固版 `__fget_files_rcu()`；不退回早期上游较旧的文件表获取实现。
- 系统调用接线仅针对本设备使用的 ARM64 原生和 AArch32 compat，以及 asm-generic/tools 镜像；未宣称完成各独立架构原生内核接线。

## 上游来源

- openat2：[fddb5d430ad9](https://github.com/torvalds/linux/commit/fddb5d430ad9fa91b49b1d34d0202ffe2fa0e179)。namei 前置：`1bc82070fa27`、`740a16782750`、`278121417a72`、`4b99d4996979`、`72ba29297e14`、`adb21d2b526f`、`8db52c7e7ee1`、`ab87f9a56c8e`。
- 高位标志修正：[cfe80306a0dd](https://github.com/torvalds/linux/commit/cfe80306a0dd6d363934913e47c3f30d71b721e5)；作用域互斥：`398840f8bb93`；目录创建组合：[43b450632676](https://github.com/torvalds/linux/commit/43b450632676fb60e9faeddff285d9fac94a4f58)。
- faccessat2：[c8ffd8bcdd28](https://github.com/torvalds/linux/commit/c8ffd8bcdd28296a198f237cc595148a8d4adfbe)。
- 凭据优化：[981ee95cc1f5](https://github.com/torvalds/linux/commit/981ee95cc1f5905ae4936b0dd501085909cdc14f)。能力集比较适配为 5.4 的 u32 数组表示，不改变 kernel_cap_t ABI。
- pidfd_getfd：[8649c322f75c](https://github.com/torvalds/linux/commit/8649c322f75c96e7ced2fec201e123b2b073bf09)；fget_task：`5e876fb43dbf`；socket 记账：`4969f8a07397`。
- exec 同步：[eea9673250db](https://github.com/torvalds/linux/commit/eea9673250db4e854e9998ef9da6d4584857f0ea)、`501f9328bf5c`、`a28bf136e651`。保留适合旧 binfmt 错误路径的 free_bprm 解锁机制，没有套用依赖后续 exec 大重构的释放方式。
- `openat2_test`、`helpers` 和 `rename_attack_test` 取自上述本地 Android 6.6 固定提交，保留原作者/许可证；测试目录改为私有 TMPDIR 子目录，直接引用本树 openat2 UAPI，补 ARM32 标志和测试清理。

## 构建与验证

构建命令：`JOBS=8 ./build_venus.sh`。保持原输出目录 `out-venus-5.4.302`，KPM 关闭；MGLRU 仍是编译可用、默认关闭。首轮编译发现并补齐 `cap_isidentical()` 与 socket classid 头文件依赖，日志保留首轮报错及后续构建记录。

构建状态：完整构建成功（退出码 0），2026-09-05 08:11 CST 生成 Image / Image.gz；`gzip -t`、`git diff --check` 通过。新增核心源码检查为 0 errors，保留 6 项主要来自上游样式的 checkpatch 提示；构建中还有未修改的厂商驱动警告，不是零警告构建。

- 内核标识：`5.4.302-DSL-qgki-g78c1b7cf #5 SMP PREEMPT Sat Sep 5 08:09:14 CST 2026`。
- `out-venus-5.4.302/arch/arm64/boot/Image`：46336512 字节，SHA256 `980eced15d5b15601a315b79e9347a7cb3cfd6ed77263db8c5ea7124526c2123`。
- `out-venus-5.4.302/arch/arm64/boot/Image.gz`：20940991 字节，SHA256 `de18c994c728a601eedd5a33aa48f989c71939f022eb004ce852891ec337ad31`。
- 最终 vmlinux 的 `sys_call_table` 和 `compat_sys_call_table` 第 437/438/439 项均通过实际指针核对，分别指向 `__arm64_sys_openat2`、`__arm64_sys_pidfd_getfd`、`__arm64_sys_faccessat2`，不是仅检查 UAPI 常量或符号是否存在。
- 构建日志：`build_78c1b7cf4_openat2_pidfd_access_20260905.log`（前部保留首次失败，后部为修正后成功构建）。

自测源码位于 `tools/testing/selftests/openat2/`，使用原树 kselftest 框架。

- `openat2_test`：116 项结构体/标志测试。
- `venus_fs_compat_test`：路径限制、proc magic link、跨挂载拒绝、失败创建无副作用、旧 openat 规则、faccessat2 权限语义、pidfd CLOEXEC/共享偏移/socket 等检查。
- `rename_attack_test`：BENEATH 和 IN_ROOT 各 400000 次并发重命名检查。仅在支持 openat2 时测试，不以普通 openat 的结果代替。

产物目录：`out-venus-5.4.302/selftests/{host,arm64,arm32}`。Android 测试程序以 NDK r27c / API 30 静态链接；两种 ABI 均编译通过。宿主机运行的是 `6.8.0-138-generic`，其结果只验证测试程序及预期语义，**不等于回移后的 5.4 内核运行结果**。

宿主测试结果：openat2 参数 116/116 通过；VFS/pidfd 综合 35 通过、1 项 root UID 分离测试跳过；重命名竞争 2/2 通过（共 800000 次，无越界结果和异常错误）。三组测试均退出码 0，TAP 日志保存在 host 产物目录。

手机刷入后，需要在 ARM64 与 ARM32 下分别运行三个测试程序，设置 `TMPDIR=/data/local/tmp`。使用 root 才能执行 real/effective UID 分离测试；保留当前 SELinux 状态，若 seccomp/SELinux 阻止调用，应单独记录，不能将权限跳过当成功。

仍待手机验证：冷启动、应用启动/安装、KSU/SuSFS 回归、AOD 单击/双击/FOD、后台并发 exec 与 pidfd 压力，以及实际 access 开销和待机功耗。本轮不刷机、不修改手机系统、不提交或推送 Git。
