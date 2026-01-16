# Get the directory where this common.mk file is located
CONFIG_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Library Locations (allow environment override)
LIBDAISY_DIR ?= $(CONFIG_DIR)libDaisy

# Normalize paths for cross-platform compatibility
normalize_path = $(subst \,/,$(1))
LIBDAISY_DIR := $(call normalize_path,$(LIBDAISY_DIR))

# Verify critical directories exist
$(if $(wildcard $(LIBDAISY_DIR)),,$(error libDaisy directory not found at $(LIBDAISY_DIR)))

# Define subdirectories
SYSTEM_FILES_DIR := $(LIBDAISY_DIR)/core

# Set Boot Bin to 10ms version (default is 2000ms)
BOOT_BIN = $(SYSTEM_FILES_DIR)/dsy_bootloader_v6_3-intdfu-10ms.bin

# Set app type
APP_TYPE = BOOT_SRAM

# Set the C++ standard
CPP_STANDARD = -std=gnu++14

# Set optimization level
OPT=-Ofast

# Core location, and generic makefile.
include $(SYSTEM_FILES_DIR)/Makefile

# Debug information (can be disabled by setting VERBOSE=0)
ifneq ($(VERBOSE),0)
$(info CONFIG_DIR: $(CONFIG_DIR))
$(info LIBDAISY_DIR: $(LIBDAISY_DIR))
endif