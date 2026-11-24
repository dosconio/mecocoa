# ASCII Makefile TAB4 LF
# GNU Arm Embedded GCC build for Mecocoa on the STM32H750 Fanke board.
.DEFAULT_GOAL := build
.DELETE_ON_ERROR:

arch := stm32h750X-ac7m
H750_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
BOARD_DIR := $(abspath $(dir $(H750_MAKEFILE)))
REPO_ROOT := $(abspath $(BOARD_DIR)/../../../..)

ifndef uincpath
$(error Set uincpath to the Unisym include directory)
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

UNISYM_H7_LIB ?= $(ubinpath)/libSTM32H7.a
FREETYPE_AC7M_LIB ?= $(REPO_ROOT)/depends/freetype/x86/libfreetype-ac7m-gcc.a
BUILD_DIR := $(uobjpath)/mcca-$(arch)-gcc
OUTPUT := $(ubinpath)/mcca-$(arch)-gcc
ELF := $(OUTPUT).elf
BIN := $(OUTPUT).bin
HEX := $(OUTPUT).hex
MAP := $(OUTPUT).map
LD_SCRIPT := $(BOARD_DIR)/mcca.ld

CPUFLAGS := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
OPT ?= -O1
CPPFLAGS += -I. -I$(uincpath) -I$(REPO_ROOT)/depends/freetype/x86/include
CPPFLAGS += -D_MCU_STM32H7x -D_HIS_IMPLEMENT -D_MCCA=0x1A072032 -D_OPT_ARM32 -D_DEBUG
CXXFLAGS += $(CPUFLAGS) $(OPT) -g3 -std=gnu++20 -ffreestanding -nostdinc++
CXXFLAGS += -fno-builtin -fno-strict-aliasing -fno-stack-protector -fno-pic -fno-pie
CXXFLAGS += -fno-exceptions -fno-rtti -fno-use-cxa-atexit -ffunction-sections -fdata-sections
CXXFLAGS += -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-parentheses -Wno-multichar
CXXFLAGS += -Wno-old-style-cast -Wno-extra-semi -Wno-deprecated-copy -Wno-empty-body
CXXFLAGS += -Wno-address -Wno-nonnull-compare
CXXFLAGS += -fno-threadsafe-statics
ASFLAGS += $(CPUFLAGS) -g3 -x assembler-with-cpp
LDFLAGS += $(CPUFLAGS) -nostdlib -static -T$(LD_SCRIPT)
LDFLAGS += -Wl,--gc-sections -Wl,--build-id=none -Wl,-Map=$(MAP)

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
	mecocoa/console.cpp \
	mecocoa/console-bcon.cpp \
	mecocoa/console-init.cpp \
	mecocoa/fileman.cpp \
	mecocoa/filesys.cpp \
	mecocoa/graphic.cpp \
	mecocoa/schedul.cpp \
	archits/arm32/cortex-m/arch-schedul.cpp \
	mecocoa/syscall.cpp \
	mecocoa/sysinfo.cpp \
	mecocoa/sysinfo-dev.cpp \
	mecocoa/systime.cpp \
	mecocoa/userman.cpp \
	mecocoa/devsman.cpp \
	mecocoa/devsman-usb.cpp \
	mecocoa/threads.cpp \
	mecocoa/graphic-touch.cpp \
	prehost/prehost.cpp \
	prehost/stm32h7-ac7m/main.cpp \
	prehost/stm32h7-ac7m/boards/h750-fanke/h750-fanke.cpp \
	prehost/stm32h7-ac7m/mem.cpp \
	devdriv/storage/sdcard.cpp \
	devdriv/tty/serial.cpp \
	devdriv/video/video-ltdc.cpp \
	devdriv/touch/touch-gtxxx.cpp \
	devdriv/bus/bus-gpio.cpp \
	devdriv/mem/fmc-sdram.cpp \
	depends/desktop.cpp \
	depends/freetype.cpp \
	depends/cotlabx.cpp \
	prehost/_auxiliary.cpp
ASM_SOURCES := stm32h750.gcc.S stm32h750.startup.gcc.S
CPP_OBJECTS := $(foreach src,$(CPP_SOURCES),$(BUILD_DIR)/$(dir $(src))_x_$(notdir $(src:.cpp=.o)))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/board/,$(ASM_SOURCES:.S=.o))
OBJECTS := $(CPP_OBJECTS) $(ASM_OBJECTS)
DEPS := $(OBJECTS:.o=.d)

.PHONY: build clean
build: $(ELF) $(BIN) $(HEX)
	@$(SIZE) $(ELF)

$(UNISYM_H7_LIB):
	@echo "Missing GCC H7 system library: $@" >&2
	@echo "Build Unisym's H7 GCC library first, or set UNISYM_H7_LIB." >&2
	@exit 1

$(FREETYPE_AC7M_LIB):
	@echo "Missing FreeType Cortex-M7 library: $@" >&2
	@echo "Build depends/freetype/x86/libfreetype-ac7m-gcc.a first, or set FREETYPE_AC7M_LIB." >&2
	@exit 1

$(ELF): $(OBJECTS) $(FREETYPE_AC7M_LIB) $(UNISYM_H7_LIB) $(LD_SCRIPT) $(H750_MAKEFILE)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	@$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) -Wl,--start-group \
		$(FREETYPE_AC7M_LIB) $(UNISYM_H7_LIB) -lgcc -Wl,--end-group

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@

define h750_cpp_to_o
$(BUILD_DIR)/$(dir $(1))_x_$(notdir $(1:.cpp=.o)): $(REPO_ROOT)/$(1) $(H750_MAKEFILE)
	@mkdir -p $$(dir $$@)
	@echo "CX $$<"
	@$$(CXX) $$(CPPFLAGS) $$(CXXFLAGS) -MMD -MP -MF $$(@:.o=.d) -MT $$@ -c $$< -o $$@
endef
$(foreach src,$(CPP_SOURCES),$(eval $(call h750_cpp_to_o,$(src))))

$(BUILD_DIR)/board/%.o: %.S $(H750_MAKEFILE)
	@mkdir -p $(dir $@)
	@echo "AS $<"
	@$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

clean:
	rm -f $(OBJECTS) $(DEPS) $(ELF) $(BIN) $(HEX) $(MAP)

-include $(DEPS)
