## Xiaomi SM8350 Device MiYume HyperOSKernel ##

基于上游https://github.com/hushangda/android_kernel_xiaomi_sm8350_venus

## 支持设备 ##
- Xiaomi 11 (venus)
- Xiaomi 11 Pro (mars)
- Xiaomi 11 Ultra (star)

## 内核特性 ##
- 内核版本：5.4.302
- 集成 ReSukiSU + SuSFS
- Backport BPF (Kernel5.10)
- 启用 LTO (ThinLTO) 优化
- 测试 Android 13 含更高正常

## 需要注意 ##
- **Venus** 使用 `venus_defconfig`
- **Star/Mars** 使用 `star_defconfig`

## 工具配置 ##
- Clang 18.1.8 & LLD 18.1.8

## 配置内核 ##
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out xxx_defconfig

## 编译内核 ##
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out modules Image
