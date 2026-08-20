#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KPM="${KPM:-1}"
if [[ "$KPM" =~ ^(1|y|Y|yes|YES|true|TRUE|on|ON)$ ]]; then
  KPM=1
elif [[ "$KPM" =~ ^(0|n|N|no|NO|false|FALSE|off|OFF)$ ]]; then
  KPM=0
else
  echo "ERROR: KPM must be 1 or 0" >&2
  exit 1
fi
JOBS="${JOBS:-$(nproc)}"
if (( KPM )); then
  DEFAULT_OUT_DIR="$ROOT_DIR/out-venus-5.4.302-kpm"
else
  DEFAULT_OUT_DIR="$ROOT_DIR/out-venus-5.4.302"
fi
OUT_DIR="${OUT_DIR:-$DEFAULT_OUT_DIR}"

export ARCH=arm64
export SUBARCH=arm64
export LLVM=1
export LLVM_IAS=1
export CC=clang
export LD=ld.lld
export AR=llvm-ar
export NM=llvm-nm
export OBJCOPY=llvm-objcopy
export OBJDUMP=llvm-objdump
export STRIP=llvm-strip
export READELF=llvm-readelf
# ReSukiSU multi-manager mode works through APK signature matching. Do not pass
# KSU_MANAGER_PACKAGE, or the kernel filters managers by one package name first.
unset KSU_MANAGER_PACKAGE

MAKE_ARGS=(
  O="$OUT_DIR"
  ARCH="$ARCH"
  SUBARCH="$SUBARCH"
  LLVM="$LLVM"
  LLVM_IAS="$LLVM_IAS"
  CC="$CC"
  LD="$LD"
  AR="$AR"
  NM="$NM"
  OBJCOPY="$OBJCOPY"
  OBJDUMP="$OBJDUMP"
  STRIP="$STRIP"
  READELF="$READELF"
)

cd "$ROOT_DIR"
mkdir -p "$OUT_DIR"

make "${MAKE_ARGS[@]}" venus_defconfig
CONFIG_ARGS=(
  --enable ZRAM \
  --enable CRYPTO_LZ4 \
  --enable ZRAM_DEF_COMP_LZ4 \
  --disable ZRAM_DEF_COMP_LZORLE \
  --disable ZRAM_DEF_COMP_LZO \
  --disable ZRAM_DEF_COMP_ZSTD \
  --disable ZRAM_DEF_COMP_LZ4HC \
  --disable ZRAM_DEF_COMP_842 \
  --enable F2FS_FS_COMPRESSION \
  --enable F2FS_FS_LZO \
  --enable F2FS_FS_LZORLE \
  --enable F2FS_FS_LZ4 \
  --enable F2FS_FS_LZ4HC \
  --enable F2FS_FS_ZSTD \
  --enable F2FS_UNFAIR_RWSEM \
  --enable F2FS_CP_OPT \
  --enable KSU \
  --set-str KSU_FULL_NAME_FORMAT "%TAG_NAME%-%COMMIT_SHA%@%REPO_NAME%" \
  --enable KSU_MULTI_MANAGER_SUPPORT \
  --enable KSU_DISABLE_IN_RECOVERY \
  --disable KSU_TRACEPOINT_HOOK \
  --disable KSU_MANUAL_HOOK \
  --enable KSU_SUSFS \
  --enable KSU_SUSFS_SUS_PATH \
  --enable KSU_SUSFS_SUS_MOUNT \
  --enable KSU_SUSFS_SUS_KSTAT \
  --enable KSU_SUSFS_SPOOF_UNAME \
  --enable KSU_SUSFS_ENABLE_LOG \
  --enable KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS \
  --enable KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG \
  --enable KSU_SUSFS_OPEN_REDIRECT \
  --enable KSU_SUSFS_SUS_MAP \
  --enable KALLSYMS \
  --enable KALLSYMS_ALL \
  --enable BPF_STREAM_PARSER
)

if (( KPM )); then
  CONFIG_ARGS+=(--enable KPM)
else
  CONFIG_ARGS+=(--disable KPM)
fi

"$ROOT_DIR/scripts/config" --file "$OUT_DIR/.config" "${CONFIG_ARGS[@]}"
make "${MAKE_ARGS[@]}" olddefconfig
grep -q '^CONFIG_ZRAM_DEF_COMP="lz4"$' "$OUT_DIR/.config" || {
  echo "ERROR: ZRAM default compressor is not lz4" >&2
  exit 1
}
for required_config in \
  'CONFIG_F2FS_FS_COMPRESSION=y' \
  'CONFIG_F2FS_FS_LZO=y' \
  'CONFIG_F2FS_FS_LZORLE=y' \
  'CONFIG_F2FS_FS_LZ4=y' \
  'CONFIG_F2FS_FS_LZ4HC=y' \
  'CONFIG_F2FS_FS_ZSTD=y' \
  'CONFIG_F2FS_UNFAIR_RWSEM=y' \
  'CONFIG_F2FS_CP_OPT=y'; do
  grep -q "^$required_config$" "$OUT_DIR/.config" || {
    echo "ERROR: missing $required_config" >&2
    exit 1
  }
done
for required_config in \
  'CONFIG_KSU=y' \
  'CONFIG_KSU_MULTI_MANAGER_SUPPORT=y' \
  'CONFIG_KSU_DISABLE_IN_RECOVERY=y' \
  'CONFIG_KSU_SUSFS=y' \
  'CONFIG_KSU_SUSFS_SUS_PATH=y' \
  'CONFIG_KSU_SUSFS_SUS_MOUNT=y' \
  'CONFIG_KSU_SUSFS_SUS_KSTAT=y' \
  'CONFIG_KSU_SUSFS_SPOOF_UNAME=y' \
  'CONFIG_KSU_SUSFS_ENABLE_LOG=y' \
  'CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS=y' \
  'CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG=y' \
  'CONFIG_KSU_SUSFS_OPEN_REDIRECT=y' \
  'CONFIG_KSU_SUSFS_SUS_MAP=y'; do
  grep -q "^$required_config$" "$OUT_DIR/.config" || {
    echo "ERROR: missing $required_config" >&2
    exit 1
  }
done
grep -q '^# CONFIG_KSU_TRACEPOINT_HOOK is not set$' "$OUT_DIR/.config" || {
  echo "ERROR: CONFIG_KSU_TRACEPOINT_HOOK must be disabled for this 5.4 tree" >&2
  exit 1
}
grep -q '^# CONFIG_KSU_MANUAL_HOOK is not set$' "$OUT_DIR/.config" || {
  echo "ERROR: CONFIG_KSU_MANUAL_HOOK must be disabled for ReSukiSU SuSFS inline mode" >&2
  exit 1
}
if (( KPM )); then
  grep -q '^CONFIG_KPM=y$' "$OUT_DIR/.config" || {
    echo "ERROR: CONFIG_KPM is not enabled" >&2
    exit 1
  }
else
  grep -q '^# CONFIG_KPM is not set$' "$OUT_DIR/.config" || {
    echo "ERROR: CONFIG_KPM must be disabled for this build" >&2
    exit 1
  }
fi

rm -f "$OUT_DIR/kernel/config_data" \
      "$OUT_DIR/kernel/config_data.gz" \
      "$OUT_DIR/kernel/configs.o" \
      "$OUT_DIR/kernel/.configs.o.cmd"
make -j"$JOBS" "${MAKE_ARGS[@]}" Image Image.gz dtbs

if grep -aq 'com\.resukisu\.resukisu' "$OUT_DIR/vmlinux"; then
  echo "ERROR: manager package restriction is still embedded in vmlinux" >&2
  exit 1
fi

if [[ -x "$ROOT_DIR/scripts/extract-ikconfig" ]]; then
  EMBEDDED_CONFIG="$OUT_DIR/.config.embedded"
  "$ROOT_DIR/scripts/extract-ikconfig" "$OUT_DIR/arch/arm64/boot/Image.gz" > "$EMBEDDED_CONFIG"
  cmp -s "$OUT_DIR/.config" "$EMBEDDED_CONFIG" || {
    echo "ERROR: Image.gz embedded IKCONFIG does not match $OUT_DIR/.config" >&2
    diff -u --label ".config" "$OUT_DIR/.config" \
      --label "Image.gz IKCONFIG" "$EMBEDDED_CONFIG" | sed -n '1,120p' >&2 || true
    exit 1
  }
fi

KERNEL_IMAGE="$OUT_DIR/arch/arm64/boot/Image"
KERNEL_IMAGE_GZ="$OUT_DIR/arch/arm64/boot/Image.gz"
if [[ -n "${TWRP_DEVICE_DIR:-}" ]]; then
  TWRP_DEVICE_DIR="$(realpath "$TWRP_DEVICE_DIR")"
  if [[ ! -f "$TWRP_DEVICE_DIR/BoardConfig.mk" ||
        ! -d "$TWRP_DEVICE_DIR/prebuilt/venus" ]]; then
    echo "ERROR: TWRP_DEVICE_DIR must point to device/xiaomi/venus" >&2
    exit 1
  fi
  install -m 0644 "$KERNEL_IMAGE" "$TWRP_DEVICE_DIR/prebuilt/venus/kernel"
  if [[ -f "$TWRP_DEVICE_DIR/prebuilt/modules.load.recovery" ]]; then
    cp -a "$TWRP_DEVICE_DIR/prebuilt/modules.load.recovery" \
      "$TWRP_DEVICE_DIR/prebuilt/modules.load.recovery.bak"
    cat > "$TWRP_DEVICE_DIR/prebuilt/modules.load.recovery" <<'EOF'
# This kernel builds the Venus recovery display/touch/charger paths in-tree.
# Do not load stale prebuilt modules from the 5.4.147 TWRP kernel package.
EOF
  fi
  if grep -q '^TW_LOAD_VENDOR_MODULES[[:space:]]*:=' "$TWRP_DEVICE_DIR/BoardConfig.mk"; then
    cp -a "$TWRP_DEVICE_DIR/BoardConfig.mk" "$TWRP_DEVICE_DIR/BoardConfig.mk.bak"
    sed -i -E 's/^TW_LOAD_VENDOR_MODULES[[:space:]]*:=.*/TW_LOAD_VENDOR_MODULES := ""/' \
      "$TWRP_DEVICE_DIR/BoardConfig.mk"
    echo "TWRP vendor module autoload disabled: $TWRP_DEVICE_DIR/BoardConfig.mk"
  fi
  echo "TWRP prebuilt kernel updated: $TWRP_DEVICE_DIR/prebuilt/venus/kernel"
fi

echo "KSU manager package restriction: disabled"
echo "ReSukiSU multi-manager support: enabled"
echo "ReSukiSU + SuSFS: enabled"
echo "KPM: $([[ "$KPM" == 1 ]] && echo enabled || echo disabled)"
echo "F2FS compression: enabled"
echo "F2FS performance options: unfair rwsem + checkpoint fsync optimization enabled"
echo "Recovery/TWRP KSU userspace hook guard: enabled"
echo "Image (for TWRP prebuilt/venus/kernel): $KERNEL_IMAGE"
echo "Image.gz: $KERNEL_IMAGE_GZ"
