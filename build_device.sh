#!/bin/bash
# 用法: bash build_device.sh <设备代号>
# 例如: bash build_device.sh venus
# 不带参数默认 venus

device=${1:-venus}
workdir=$(cd "$(dirname "$0")"; pwd)
cd "$workdir"

echo "构建设备: $device"

# 配置内核
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out ${device}_defconfig

# 编译内核
make -j$(nproc --all) ARCH=arm64 LLVM=1 LLVM_IAS=1 O=out modules Image
