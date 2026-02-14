##################################################################################
# Makefile for Breezehand Overlay
# Author: tomvita
# Description:
#   This Makefile is used to build the Breezehand Overlay homebrew application for
#   Nintendo Switch.
#
#   For more details and usage instructions, please refer to the project's
#   documentation and README.md.
#
#   GitHub Repository: https://github.com/tomvita/Breezehand-Overlay
#
# Licensed under GPLv2
# Copyright (c) 2023-2026 ppkantorski
##################################################################################

#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------


ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules


#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS, relative to the Makefile (Optional)
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# APP_TITLEID is the titleID of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#	 - <Project name>.jpg
#	 - icon.jpg
#	 - <libnx folder>/default_icon.jpg
#
# CONFIG_JSON is the filename of the NPDM config file (.json), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#	 - <Project name>.json
#	 - config.json
#   If a JSON file is provided or autodetected, an ExeFS PFS0 (.nsp) is built instead
#   of a homebrew executable (.nro). This is intended to be used for sysmodules.
#   NACP building is skipped as well. #lib/Atmosphere-libs/libexosphere/source/pmic
#---------------------------------------------------------------------------------
APP_TITLE	:= Breezehand
APP_AUTHOR	:= tomvita
APP_VERSION	:= 0.8.6
TARGET		:= breezehand
BUILD		:= build
SOURCES		:= source common ../capstone ../capstone/arch/AArch64
INCLUDES	:= source common include ../capstone/include/capstone ../capstone/include/
NO_ICON		:= 1

# This location should reflect where you place the libultrahand directory (lib can vary between projects).
include ${TOPDIR}/lib/libultrahand/ultrahand.mk


#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+simd+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -g -Wall -Os -ffunction-sections -fdata-sections -flto \
          -fuse-linker-plugin -fomit-frame-pointer -finline-small-functions \
          -fno-strict-aliasing -frename-registers -falign-functions=16 \
          $(ARCH) $(DEFINES)

CFLAGS += $(INCLUDE) -D__SWITCH__ -DAPP_VERSION="\"$(APP_VERSION)\"" -D_FORTIFY_SOURCE=2 -DCAPSTONE_HAS_ARM64 -DCAPSTONE_USE_SYS_DYN_MEM -DCAPSTONE_DIET -DCAPSTONE_STATIC


#---------------------------------------------------------------------------------
# options for libultrahand
#---------------------------------------------------------------------------------
# For compiling Ultrahand Overlay only
IS_LAUNCHER_DIRECTIVE := 1
CFLAGS += -DIS_LAUNCHER_DIRECTIVE=$(IS_LAUNCHER_DIRECTIVE)

# Enable Widget
USING_WIDGET_DIRECTIVE := 1  # or true
CFLAGS += -DUSING_WIDGET_DIRECTIVE=$(USING_WIDGET_DIRECTIVE)

# Enable Logging
USING_LOGGING_DIRECTIVE := 1  # or true
CFLAGS += -DUSING_LOGGING_DIRECTIVE=$(USING_LOGGING_DIRECTIVE)

# FPS Indicator (for debugging)
USING_FPS_INDICATOR_DIRECTIVE := 0
CFLAGS += -DUSING_FPS_INDICATOR_DIRECTIVE=$(USING_FPS_INDICATOR_DIRECTIVE)

ifeq ($(strip $(TARGET)),editcheat)
    CFLAGS += -DEDITCHEAT_OVL
endif

# Enable fstream (ideally for other overlays want full fstream instead of FILE*)
#USING_FSTREAM_DIRECTIVE := 0
#CFLAGS += -DUSING_FSTREAM_DIRECTIVE=$(USING_FSTREAM_DIRECTIVE)
# Optional: use Keystone as ASM backend (broader instruction coverage).
# Disabled by default to keep binary/memory overhead low.
USE_KEYSTONE_ASM ?= 0
CFLAGS += -DUSE_KEYSTONE_ASM=$(USE_KEYSTONE_ASM)
# Local Keystone checkout root (override if needed).
KEYSTONE_ROOT ?= $(TOPDIR)/../keystone
ifeq ($(wildcard $(KEYSTONE_ROOT)/include/keystone/keystone.h),)
ifneq ($(wildcard /c/GitHub/keystone/include/keystone/keystone.h),)
KEYSTONE_ROOT := /c/GitHub/keystone
endif
endif
ifeq ($(wildcard $(KEYSTONE_ROOT)/include/keystone/keystone.h),)
ifneq ($(wildcard /mnt/c/GitHub/keystone/include/keystone/keystone.h),)
KEYSTONE_ROOT := /mnt/c/GitHub/keystone
endif
endif
#---------------------------------------------------------------------------------


CXXFLAGS := $(CFLAGS) -std=c++26 -Wno-dangling-else -ffast-math -fno-unwind-tables -fno-asynchronous-unwind-tables 

ASFLAGS := $(ARCH)
LDFLAGS += -flto -fuse-linker-plugin -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(notdir $*.map)

# Essential libraries for Ultrahand Overlay
LIBS := -lcurl -lz -lminizip -lmbedtls -lmbedx509 -lmbedcrypto -lnx
ifeq ($(strip $(USE_KEYSTONE_ASM)),1)
LIBS += -lkeystone
INCLUDES += $(KEYSTONE_ROOT)/include
LIBDIRS += $(KEYSTONE_ROOT)
CFLAGS += -I$(KEYSTONE_ROOT)/include
LDFLAGS += -L$(KEYSTONE_ROOT)/lib
endif

CXXFLAGS += -fno-exceptions -ffunction-sections -fdata-sections -fno-rtti
LDFLAGS += -Wl,--as-needed -Wl,--gc-sections

# For Ensuring Parallel LTRANS Jobs w/ GCC, make -j N (for convenience)
# ------------------------------------------------------------
# Detect how many logical CPUs are available, cross-platform
# ------------------------------------------------------------
NPROC := $(shell \
	getconf _NPROCESSORS_ONLN 2>/dev/null || \
	nproc 2>/dev/null || \
	sysctl -n hw.ncpu 2>/dev/null || \
	echo 1)

# Fallback if detection somehow failed
ifeq ($(strip $(NPROC)),)
	NPROC := 1
endif

# ------------------------------------------------------------
# Set parallelism flags only if user didn't pass -j manually
# ------------------------------------------------------------
ifeq (,$(filter -j -j%,$(MAKEFLAGS)))
	MAKEFLAGS += -j$(NPROC)
endif

# ------------------------------------------------------------
# Enable link-time optimization with parallel jobs
# ------------------------------------------------------------
CXXFLAGS += -flto=$(NPROC)
LDFLAGS  += -flto=$(NPROC)


# Add -z notext to LDFLAGS to allow dynamic relocations in read-only segments
#LDFLAGS += -z notext

# For Ensuring Parallel LTRANS Jobs w/ Clang, make -j6
#CXXFLAGS += -flto -flto-jobs=6
#LDFLAGS += -flto-jobs=6

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(PORTLIBS) $(LIBNX)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD := $(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD := $(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),$(if $(filter /% c:% C:% %:%,$(dir)),-I$(dir),-I$(CURDIR)/$(dir))) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all release full breezehand editcheat

#---------------------------------------------------------------------------------
all: breezehand editcheat


breezehand: $(BUILD)

editcheat:
	@mkdir -p build_editcheat
	@rm -f editcheat.nacp
	@$(MAKE) --no-print-directory -C build_editcheat -f $(CURDIR)/Makefile \
		TARGET=editcheat \
		BUILD=build_editcheat \
		APP_TITLE="Edit Cheat" \
		APP_VERSION="1.0.0" \
		APP_JSON= \
		OUTPUT=editcheat \
		DEPSDIR=$(CURDIR)/build_editcheat \
		NROFLAGS= \
		TOPDIR=$(CURDIR) \
		USE_KEYSTONE_ASM=0 \
		KEYSTONE_ROOT=$(CURDIR)/../keystone \
		INCLUDE="$(INCLUDE) -I$(CURDIR)/../keystone/include" \
		LIBPATHS="$(LIBPATHS) -L$(CURDIR)/../keystone/lib" \
		MAKEFLAGS="$(filter-out -j% -j,$(MAKEFLAGS)) -j"
	@mkdir -p build_editcheatk
	@rm -f editcheatk.nacp
	@$(MAKE) --no-print-directory -C build_editcheatk -f $(CURDIR)/Makefile \
		TARGET=editcheat \
		BUILD=build_editcheatk \
		APP_TITLE="Edit Cheat" \
		APP_VERSION="1.0.0" \
		APP_JSON= \
		OUTPUT=editcheatk \
		DEPSDIR=$(CURDIR)/build_editcheatk \
		NROFLAGS= \
		TOPDIR=$(CURDIR) \
		USE_KEYSTONE_ASM=1 \
		KEYSTONE_ROOT=$(CURDIR)/../keystone \
		INCLUDE="$(INCLUDE) -I$(CURDIR)/../keystone/include" \
		LIBPATHS="$(LIBPATHS) -L$(CURDIR)/../keystone/lib" \
		MAKEFLAGS="$(filter-out -j% -j,$(MAKEFLAGS)) -j"
	@mkdir -p out/switch/.overlays/
	@rm -f out/switch/.overlays/editasm.ovl out/switch/.overlays/edittasmk.ovl
	@cp build_editcheat/editcheat.ovl out/switch/.overlays/editcheat.ovl
	@cp build_editcheatk/editcheatk.ovl out/switch/.overlays/editcheatk.ovl

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile MAKEFLAGS="$(filter-out -j% -j,$(MAKEFLAGS)) -j"

	@rm -rf out/
	@mkdir -p out/switch/.overlays/
	@mkdir -p out/config/breezehand/lang/
	@cp $(CURDIR)/$(TARGET).ovl out/switch/.overlays/$(TARGET).ovl
	@cp $(CURDIR)/cheat_url_txt.template out/config/breezehand/cheat_url_txt.template
	@cp $(CURDIR)/lang/*.json out/config/breezehand/lang/

#---------------------------------------------------------------------------------
clean:
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nro $(TARGET).nacp $(TARGET).elf
	@rm -fr build_editcheat build_editcheatk editcheat.ovl editcheat.nro editcheat.nacp editcheat.elf editcheatk.ovl editcheatk.nro editcheatk.nacp editcheatk.elf

	@rm -rf out/
	@rm -f $(TARGET).zip

#---------------------------------------------------------------------------------
dist: all
	@echo making dist ...

	@rm -f $(TARGET).zip
	@cd out; zip -r ../$(TARGET).zip ./*; cd ../

release: dist

full: all
	@echo making full release ...
	@python sdout.py
#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all : $(OUTPUT).ovl

$(OUTPUT).ovl: $(OUTPUT).elf $(OUTPUT).nacp 
	@elf2nro $< $@ $(NROFLAGS)
	@echo "built ... $(notdir $(OUTPUT).ovl)"
	@printf 'ULTR' >> $@
	@printf "Ultrahand signature has been added.\n"


$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o %_bin.h : %.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
