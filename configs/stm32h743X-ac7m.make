# ASCII Makefile TAB4 LF
# GNU Arm Embedded GCC build for Mecocoa STM32H743, not the Keil project.
# Run from the repository root; uincpath/ulibpath/uobjpath/ubinpath are explicit inputs.
.DEFAULT_GOAL := build
.DELETE_ON_ERROR:

arch := stm32h743X-ac7m
H7_MAKEFILE := $(lastword $(MAKEFILE_LIST))
H7_DIR := prehost/$(arch)

ifndef uincpath
$(error Set uincpath to the Unisym include directory)
endif
ifndef ulibpath
$(error Set ulibpath to the Unisym source library directory)
endif
ifndef uobjpath
$(error Set uobjpath to the object output directory)
endif
ifndef ubinpath
$(error Set ubinpath to the binary output directory)
endif

CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE = $(CROSS_COMPILE)size

# This archive must be built with GCC for the same CPU and floating-point ABI.
UNISYM_H7_LIB ?= $(ubinpath)/libSTM32H7.a
BUILD_DIR := $(uobjpath)/mcca-$(arch)-gcc
OUTPUT := $(ubinpath)/mcca-$(arch)-gcc
ELF := $(OUTPUT).elf
BIN := $(OUTPUT).bin
HEX := $(OUTPUT).hex
MAP := $(OUTPUT).map
LD_SCRIPT := $(H7_DIR)/mcca.ld

CPUFLAGS := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
OPT ?= -O1
CPPFLAGS += -I$(uincpath) -D_MCU_STM32H7x -D_HIS_IMPLEMENT -D_MCCA=0x1A072032 -D_OPT_ARM32 -D_DEBUG
CXXFLAGS += $(CPUFLAGS) $(OPT) -g3 -std=gnu++20 -ffreestanding -nostdinc++
CXXFLAGS += -fno-builtin -fno-strict-aliasing -fno-stack-protector -fno-pic -fno-pie
CXXFLAGS += -fno-exceptions -fno-rtti -fno-use-cxa-atexit -ffunction-sections -fdata-sections
CXXFLAGS += -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-parentheses -Wno-multichar
# Match main.uvprojx suppressions using GCC diagnostic names; omit Clang-only options.
CXXFLAGS += -Wno-old-style-cast -Wno-extra-semi -Wno-deprecated-copy -Wno-empty-body
# GCC diagnoses always-true address tests and nonnull argument comparisons separately.
CXXFLAGS += -Wno-address -Wno-nonnull-compare
CXXFLAGS +=   -fno-threadsafe-statics
ASFLAGS += $(CPUFLAGS) -g3 -x assembler-with-cpp
LDFLAGS += $(CPUFLAGS) -nostdlib -static -T$(LD_SCRIPT)
LDFLAGS += -Wl,--gc-sections -Wl,--build-id=none -Wl,-Map=$(MAP)

# Explicitly mirror the H7 Keil source set; do not collect other platforms' drivers.
CPP_SOURCES := \
	mecocoa/memoman.cpp \
	mecocoa/memoman-init.cpp \
	mecocoa/memoman-swap.cpp \
	mecocoa/message.cpp \
	mecocoa/taskman.cpp \
	mecocoa/taskman-new.cpp \
	mecocoa/taskman-elf.cpp \
	mecocoa/taskman-lock.cpp \
	mecocoa/handler.cpp \
	mecocoa/handler-tim.cpp \
	mecocoa/console.cpp \
	mecocoa/console-bcon.cpp \
	mecocoa/console-init.cpp \
	mecocoa/fileman.cpp \
	mecocoa/filesys.cpp \
	mecocoa/graphic.cpp \
	mecocoa/schedul.cpp \
	mecocoa/syscall.cpp \
	mecocoa/sysinfo.cpp \
	mecocoa/sysinfo-dev.cpp \
	mecocoa/devsman.cpp \
	mecocoa/devsman-usb.cpp \
	mecocoa/threads.cpp \
	mecocoa/graphic-touch.cpp \
	prehost/prehost.cpp \
	prehost/stm32h743X-ac7m/main.cpp \
	prehost/stm32h743X-ac7m/mem.cpp \
	prehost/stm32h743X-ac7m/_openedv/RGB-LCD.cpp \
	prehost/stm32h743X-ac7m/_openedv/SDRAM.cpp \
	devdriv/storage/sdcard.cpp \
	devdriv/tty/serial.cpp \
	devdriv/video/video-ltdc.cpp \
	devdriv/touch/touch-gtxxx.cpp \
	devdriv/kboard.cpp \
	devdriv/usb/usb_otg.cpp \
	devdriv/usb/usb_storage.cpp \
	devdriv/bus/bus-gpio.cpp \
	depends/desktop.cpp \
	devdriv/video/video-classic.cpp \
	prehost/_auxiliary.cpp

ASM_SOURCES := $(H7_DIR)/stm32h743.gcc.S $(H7_DIR)/stm32h743.startup.gcc.S
CPP_OBJECTS := $(addprefix $(BUILD_DIR)/,$(CPP_SOURCES:.cpp=.o))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.S=.o))
VIDEO_OBJECT := $(BUILD_DIR)/unisym/Video-VideoConsole2.o
OBJECTS := $(CPP_OBJECTS) $(ASM_OBJECTS) $(VIDEO_OBJECT)
DEPS := $(OBJECTS:.o=.d)

.PHONY: build clean
build: $(ELF) $(BIN) $(HEX)
	@$(SIZE) $(ELF)

# The archive is an input; this target does not rebuild Unisym or use Keil's .lib.
$(UNISYM_H7_LIB):
	@echo "Missing GCC H7 system library: $@" >&2
	@echo "Build Unisym's H7 GCC library first, or set UNISYM_H7_LIB." >&2
	@exit 1

$(ELF): $(OBJECTS) $(UNISYM_H7_LIB) $(LD_SCRIPT) $(H7_MAKEFILE)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	@$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) -Wl,--start-group $(UNISYM_H7_LIB) -lgcc -Wl,--end-group

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.o: %.cpp $(H7_MAKEFILE)
	@mkdir -p $(dir $@)
	@echo "CX $<"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(BUILD_DIR)/%.o: %.S $(H7_MAKEFILE)
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(VIDEO_OBJECT): $(ulibpath)/cpp/Device/Video-VideoConsole2.cpp $(H7_MAKEFILE)
	@mkdir -p $(dir $@)
	@echo "CX $<"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

clean:
	rm -f $(OBJECTS) $(DEPS) $(ELF) $(BIN) $(HEX) $(MAP)

-include $(DEPS)
