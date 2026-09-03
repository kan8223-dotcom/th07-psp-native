.DEFAULT_GOAL := all

override SHELL := /bin/sh
override .SHELLFLAGS := -eu -c
override PATH := /usr/local/pspdev/bin:/usr/bin:/bin
override PSPDEV := /usr/local/pspdev
override PSPSDK := /usr/local/pspdev/psp/sdk
export PATH PSPDEV PSPSDK

TARGET := th07_unified_launcher
OBJS := main.o model_dispatch.o xmb_selfwrap.o
EXTRA_TARGETS := EBOOT.PBP
BUILD_PRX := 1
PSP_FW_VERSION := 660
PSP_LARGE_MEMORY := 0
PSP_EBOOT_TITLE := Touhou 7 PSP

# The distributable starts with no copyrighted XMB media.  A runtime hook may
# generate a local self-wrap only after validating the user's original data.
PSP_EBOOT_ICON := NULL
PSP_EBOOT_ICON1 := NULL
PSP_EBOOT_PIC0 := NULL
PSP_EBOOT_PIC1 := NULL
PSP_EBOOT_SND0 := NULL

# This target is the plain launcher input to tools/pack_unified_pbp.py.  The
# audited outer packer adds the reserved XMB area and the dual-runtime PSAR.
PSP_EBOOT_PSAR := NULL

INCDIR := .
CFLAGS := -O2 -G0 -Wall -Wextra -Werror -DPSP
CXXFLAGS := -std=gnu++17 -fno-exceptions -fno-rtti
ASFLAGS := $(CFLAGS)
LIBS := -lpspkubridge -lpspsystemctrl_user -lpng16 -ljpeg -lz

include $(PSPSDK)/lib/build.mak
