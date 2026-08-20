# SPDX-License-Identifier: GPL-2.0-only

# auto-detect subdirs
ifneq ($(CONFIG_ARCH_QTI_VM), y)
ifeq ($(CONFIG_ARCH_KONA), y)
include $(srctree)/techpack/camera_venus/config/konacamera.conf
endif

ifeq ($(CONFIG_ARCH_LITO), y)
include $(srctree)/techpack/camera_venus/config/litocamera.conf
endif

ifeq ($(CONFIG_ARCH_BENGAL), y)
include $(srctree)/techpack/camera_venus/config/bengalcamera.conf
endif

ifeq ($(CONFIG_ARCH_MONACO), y)
include $(srctree)/techpack/camera_venus/config/monacocamera.conf
endif

ifeq ($(CONFIG_ARCH_LAHAINA), y)
include $(srctree)/techpack/camera_venus/config/lahainacamera.conf
endif

ifeq ($(CONFIG_ARCH_HOLI), y)
include $(srctree)/techpack/camera_venus/config/holicamera.conf
endif

ifeq ($(CONFIG_ARCH_BLAIR), y)
include $(srctree)/techpack/camera_venus/config/blaircamera.conf
endif

ifeq ($(CONFIG_ARCH_SHIMA), y)
include $(srctree)/techpack/camera_venus/config/shimacamera.conf
endif

ifeq ($(CONFIG_ARCH_YUPIK), y)
include $(srctree)/techpack/camera_venus/config/yupikcamera.conf
endif

ifeq ($(CONFIG_ARCH_SM6150), y)
include $(srctree)/techpack/camera_venus/config/sm6150camera.conf
endif

ifeq ($(CONFIG_ARCH_KONA), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/konacameraconf.h
endif

ifeq ($(CONFIG_ARCH_LITO), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/litocameraconf.h
endif

ifeq ($(CONFIG_ARCH_BENGAL), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/bengalcameraconf.h
endif

ifeq ($(CONFIG_ARCH_MONACO), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/monacocameraconf.h
endif

ifeq ($(CONFIG_ARCH_LAHAINA), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/lahainacameraconf.h
endif

ifeq ($(CONFIG_ARCH_HOLI), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/holicameraconf.h
endif

ifeq ($(CONFIG_ARCH_BLAIR), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/blaircameraconf.h
endif

ifeq ($(CONFIG_ARCH_SHIMA), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/shimacameraconf.h
endif

ifeq ($(CONFIG_ARCH_YUPIK), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/yupikcameraconf.h
endif

ifeq ($(CONFIG_ARCH_SM6150), y)
LINUXINCLUDE    += \
		-include $(srctree)/techpack/camera_venus/config/sm6150cameraconf.h
endif

endif

#add camera macro
ifdef CONFIG_MACH_XIAOMI_ZIJIN
    KBUILD_CPPFLAGS += -DZIJIN_CAM
endif

ifdef CONFIG_MACH_XIAOMI_TAOYAO
    KBUILD_CPPFLAGS += -DTAOYAO_CAM
endif

ifdef CONFIG_MACH_XIAOMI_REDWOOD
    KBUILD_CPPFLAGS += -DREDWOOD_CAM
endif

ifdef CONFIG_MACH_XIAOMI_HAYDN
    KBUILD_CPPFLAGS += -DHAYDN_CAM
endif

ifneq (,$(filter $(CONFIG_SPECTRA_CAMERA), y m))
# Use USERINCLUDE when you must reference the UAPI directories only.
USERINCLUDE     += \
                -I$(srctree)/techpack/camera_venus/include/uapi

# Use LINUXINCLUDE when you must reference the include/ directory.
# Needed to be compatible with the O= option
LINUXINCLUDE    += \
                -I$(srctree)/techpack/camera_venus/include/uapi \
                -I$(srctree)/techpack/camera_venus/include
obj-y += drivers/
else
$(info Target not found)
endif
