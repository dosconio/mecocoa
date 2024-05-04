# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64)
# LastCheck: 20240502
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa, on RISC-V 64
# Copyright: Dosconio Mecocoa, BCD License Version 3

# ! current path still be same with main `Makefile`

.PHONY: ciallo new newx debug dbgend clean

ARCH = riscv64
THIS := $(ARCH)
OUTD = /mnt/hgfs/_bin/mecocoa# Output Directory
OUTF = $(OUTD)/mcca-$(ARCH)# Output File

KitPrefix = $(ARCH)-unknown-elf-# their rules
CC = $(KitPrefix)gcc# contain AS
LD = $(KitPrefix)ld
OBJCOPY = $(KitPrefix)objcopy
OBJDUMP = $(KitPrefix)objdump
PYTH = python3
COPY = cp
GDB = $(KitPrefix)gdb

#20240504 do not use '~/' or makefile will say `doesn't match the target pattern`
BUILDDIR = /home/ayano/_obj/mcca
C_SRCS = $(wildcard $(THIS)/*.c)
AS_SRCS = $(wildcard $(THIS)/*.S)
C_OBJS = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(C_SRCS))))
AS_OBJS = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(AS_SRCS))))
OBJS = $(C_OBJS) $(AS_OBJS)
HEADER_DEP = $(addsuffix .d, $(basename $(C_OBJS)))

-include $(HEADER_DEP)

CFLAG = -Wall -Werror -O -fno-omit-frame-pointer -ggdb
CFLAG += -MD
CFLAG += -mcmodel=medany
CFLAG += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAG += -I$(THIS)
CFLAG += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# LOG ?= error
ifndef LOG
LOG = error
endif

ifeq ($(LOG), error)
CFLAG += -D _LOG_LEVEL_ERROR
else ifeq ($(LOG), warn)
CFLAG += -D _LOG_LEVEL_WARN
else ifeq ($(LOG), info)
CFLAG += -D _LOG_LEVEL_INFO
else ifeq ($(LOG), debug)
CFLAG += -D _LOG_LEVEL_DEBUG
else ifeq ($(LOG), trace)
CFLAG += -D _LOG_LEVEL_TRACE
else
CFLAG += -D _LOG_LEVEL_ERROR
endif

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAG += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAG += -fno-pie -nopie
endif

LDFLAG = -z max-page-size=4096

$(AS_OBJS): $(BUILDDIR)/$(THIS)/%.o : $(THIS)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAG) -c $< -o $@

$(C_OBJS): $(BUILDDIR)/$(THIS)/%.o : $(THIS)/%.c  $(BUILDDIR)/$(THIS)/%.d
	@mkdir -p $(@D)
	$(CC) $(CFLAG) -c $< -o $@

$(HEADER_DEP): $(BUILDDIR)/$(THIS)/%.d : $(THIS)/%.c
	@mkdir -p $(@D)
	@set -e; rm -f $@; $(CC) -MM $< $(INCLUDEFLAG) > $@.$$$$; \
        sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
        rm -f $@.$$$$

BOOTLOADER	:= ./depends/RustSBI/rustsbi-qemu.bin# naming: SBI + Board

QEMU = qemu-system-$(ARCH)
QEMUOPTS = \
	-nographic \
	-machine virt \
	-bios $(BOOTLOADER) \
	-kernel $(OUTF)

ciallo:
	@echo "Mecocoa Risc-V64"

new: ciallo $(OBJS)
	$(LD) $(LDFLAG) -T $(THIS)/kernel.ld -o $(OUTF) $(OBJS)
	$(OBJDUMP) -S $(OUTF) > $(OUTF).asm
	$(OBJDUMP) -t $(OUTF) | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(OUTF).sym
	@echo 'Build  : Finish'

newx: new $(OUTF)
	$(QEMU) $(QEMUOPTS)

QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::15234"; \
	else echo "-s -p 15234"; fi)

debug: new cocheck/riscv64.gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB) &
	sleep 1
	$(GDB) --init-command=cocheck/riscv64.gdbinit

dbgend:
	sudo lsof -i tcp:15234 # sudo kill -9 46080

clean:
	#rm -rf $(BUILDDIR)




