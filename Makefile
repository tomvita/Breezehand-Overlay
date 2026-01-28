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
export TOPDIR
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
APP_TITLE	?= Breezehand
APP_AUTHOR	?= tomvita
APP_VERSION	?= 0.5.1
TARGET		?= breezehand
BUILD		?= build
SOURCES		:= source common
INCLUDES	:= source common include
NO_ICON		:= 1
# list of directories containing libraries, this must be the top level containing
# include and lib
LIBDIRS := $(PORTLIBS) $(LIBNX)

# This location should reflect where you place the libultrahand directory (lib can vary between projects).
include ${TOPDIR}/lib/libultrahand/ultrahand.mk

#---------------------------------------------------------------------------------
# Define files and paths EARLY so they are available for flags
#---------------------------------------------------------------------------------
# Child target variables (don't export build-specific ones)
OUTPUT  = $(TOPDIR)/$(TARGET)
export APP_TITLE
export APP_AUTHOR
export APP_VERSION

# Common file list logic (runs in parent and sub-make)
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.s)))

ifeq ($(strip $(TARGET)),breezehand)
	CPPFILES := $(filter-out keyboard_main.cpp keyboard.cpp,$(CPPFILES))
endif
ifeq ($(strip $(TARGET)),keyboard)
	CPPFILES := $(filter-out main.cpp,$(CPPFILES))
endif

BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.*)))

# Common build variables (calculated for both parent and sub-make)
VPATH    = $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir)) \
           $(foreach dir,$(DATA),$(TOPDIR)/$(dir))
DEPSDIR  = $(TOPDIR)/$(BUILD)

ifeq ($(strip $(CPPFILES)),)
    LD := $(CC)
else
    LD := $(CXX)
endif

OFILES_BIN := $(addsuffix .o,$(BINFILES))
OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
OFILES     := $(OFILES_BIN) $(OFILES_SRC)
HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

INCLUDE := $(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
           $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
           -I$(TOPDIR)/$(BUILD)

LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)


#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+simd+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS := -g -Wall -Os -ffunction-sections -fdata-sections -flto \
          -fuse-linker-plugin -fomit-frame-pointer -finline-small-functions \
          -fno-strict-aliasing -frename-registers -falign-functions=16 \
          $(ARCH) $(DEFINES)

CFLAGS += $(INCLUDE) -D__SWITCH__ -DAPP_VERSION="\"$(APP_VERSION)\"" -D_FORTIFY_SOURCE=2


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

# Enable fstream (ideally for other overlays want full fstream instead of FILE*)
#USING_FSTREAM_DIRECTIVE := 0
#CFLAGS += -DUSING_FSTREAM_DIRECTIVE=$(USING_FSTREAM_DIRECTIVE)
#---------------------------------------------------------------------------------


CXXFLAGS := $(CFLAGS) -std=c++26 -Wno-dangling-else -ffast-math -fno-unwind-tables -fno-asynchronous-unwind-tables 

ASFLAGS := $(ARCH)
LDFLAGS += -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(notdir $*.map)

# Essential libraries for Ultrahand Overlay
LIBS := -lcurl -lz -lminizip -lmbedtls -lmbedx509 -lmbedcrypto -lnx

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

# Definitions migrated to the top


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
# Definitions migrated to the top for better dependency management

ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

# Flags and parameters are handled in the child make to allow for multi-target differences

.PHONY: clean all release full breezehand keyboard

#---------------------------------------------------------------------------------
#---------------------------------------------------------------------------------
all:
	@rm -rf out/
	@mkdir -p out/switch/.overlays/
	@mkdir -p out/config/breezehand/lang/
	@$(MAKE) --no-print-directory breezehand
	@$(MAKE) --no-print-directory keyboard
	@cp $(CURDIR)/cheat_url_txt.template out/config/breezehand/cheat_url_txt.template
	@cp $(CURDIR)/lang/*.json out/config/breezehand/lang/

breezehand:
	@mkdir -p build
	@$(MAKE) --no-print-directory -C build -f $(CURDIR)/Makefile \
		TARGET=breezehand \
		BUILD=build \
		APP_TITLE="Breezehand" \
		APP_VERSION="0.5.1" \
		MAKEFLAGS="$(filter-out -j% -j,$(MAKEFLAGS)) -j"
	@cp $(CURDIR)/breezehand.ovl out/switch/.overlays/breezehand.ovl

keyboard:
	@mkdir -p build_keyboard
	@$(MAKE) --no-print-directory -C build_keyboard -f $(CURDIR)/Makefile \
		TARGET=keyboard \
		BUILD=build_keyboard \
		APP_TITLE="Keyboard" \
		APP_VERSION="1.0.0" \
		MAKEFLAGS="$(filter-out -j% -j,$(MAKEFLAGS)) -j"
	@cp $(CURDIR)/keyboard.ovl out/switch/.overlays/keyboard.ovl

#---------------------------------------------------------------------------------
clean:
	@rm -fr $(BUILD) build_keyboard breezehand.ovl keyboard.ovl breezehand.nro keyboard.nro breezehand.nacp keyboard.nacp breezehand.elf keyboard.elf

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

# Child build definitions
ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard $(TOPDIR)/*.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard $(TOPDIR)/*.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	NROFLAGS += --nacp=$(OUTPUT).nacp
endif

ifneq ($(ROMFS),)
	NROFLAGS += --romfsdir=$(TOPDIR)/$(ROMFS)
endif

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
