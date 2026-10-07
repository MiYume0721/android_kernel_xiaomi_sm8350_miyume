## Xiaomi SM8350 Device MiYume HoshinoNeko Kernel ##

## 支持设备 ##
- Xiaomi 11 (venus)
- Xiaomi 11 Pro (mars)
- Xiaomi 11 Ultra (star)
- Redmi K40 Pro(+) (haydn)

## 内核特性 ##
- 版本：Linux 5.4.302
- BakaSU (最新 main 分支) + SuSFS v2.3.0
- Backport BPF (Kernel 5.15 UAPI & Helpers, 适配 Android 15/17 netbpfload/bpfloader)
- 启用 f2fs 文件系统优化与 Checkpoint 延迟调优
- 支持 HyperOS 4.0 Android 17 / Miui Android 13+
- 支持 GitHub Actions 自动化云端编译与 AnyKernel3 刷机包打包

## 需要注意 ##
- 低于 Android 15 开机会供应商报错
- **Venus** 使用 `venus_defconfig`
- **Star/Mars** 使用 `star_defconfig`
- **Haydn** 使用 `haydn_defconfig`

## 工具配置 ##
- Clang 18.1.8 & LLD 18.1.8

## 编译方式 ##
- **GitHub Actions (推荐)**: 仓库 Actions 页面直接运行 `Build Xiaomi SM8350 Kernel` 自动生成 AnyKernel3 刷机包。
- **本地编译**: `bash build_device.sh <设备代号>` (例如: `bash build_device.sh venus`，不填默认 venus)
