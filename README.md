## Xiaomi SM8350 Device MiYume HoshinoNeko Kernel ##

基于 https://github.com/hushangda/android_kernel_xiaomi_sm8350_venus

## 支持设备 ##
- Xiaomi 11 (venus)
- Xiaomi 11 Pro (mars)
- Xiaomi 11 Ultra (star)

## 内核特性 ##
- 版本：5.4.302
- ReSukiSU v4.2.0-rc1 + SuSFS v2.1.0
- Backport BPF (Kernel5.10)
- 启用 LTO (ThinLTO)
- 启用 f2fs 文件系统优化
- 支持 HyperOS 4.0 Android 17
- 测试 Miui Android 13 及更高正常

## 需要注意 ##
- 低于 Android 15 开机会供应商报错
- **Venus** 使用 `venus_defconfig`
- **Star/Mars** 使用 `star_defconfig`

## 工具配置 ##
- Clang 18.1.8 & LLD 18.1.8

## 开始编译 ##
bash build_device.sh (填设备代号, 不填默认venus)
