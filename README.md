## Xiaomi SM8350 Device MiYume HyperOSKernel ##

基于 https://github.com/hushangda/android_kernel_xiaomi_sm8350_venus

## 支持的设备 ##
- Xiaomi 11 (venus)
- Xiaomi 11 Pro (mars)
- Xiaomi 11 Ultra (star)

## 内核特性 ##
- 内核版本：5.4.3xx
- 集成 SukiSU + SuSFS
- Backport BPF (Kernel5.10)
- 启用 LTO (ThinLTO) 优化

## 需要注意 ##
- **Venus** 使用 `venus_defconfig`，**Star/Mars** 使用 `star_defconfig`
- **Star/Mars 用户**：需要低于 HyperOS 2.0 的 `boot.img`，否则可能导致开机时间长与震动失效
- 当前默认仅完整配置 Venus 和 Star，其他设备需自行适配

## 环境配置 ##
- Clang 18.1.8 & LLD 18.1.8

## 开始步骤 ##

# 配置
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out xxx_defconfig

# 编译
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out modules Image
