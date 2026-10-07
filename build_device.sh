#!/bin/bash
# 用法: bash build_device.sh <设备代号>
# 例如: bash build_device.sh venus
# 默认 venus

device=${1:-venus}
workdir=$(cd "$(dirname "$0")"; pwd)
cd "$workdir"

echo "目标设备: $device"

# 确保 BakaSU 5.4 兼容性补丁生效
if [ -d "KernelSU" ] && [ -f "patches/bakasu_5.4_compat.patch" ]; then
    (cd KernelSU && git apply --check ../patches/bakasu_5.4_compat.patch 2>/dev/null && git apply ../patches/bakasu_5.4_compat.patch && echo "已应用 BakaSU 5.4 兼容性补丁" || true)
fi

# 生成配置
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out_${device} ${device}_defconfig

# 编译内核
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out_${device} modules Image
