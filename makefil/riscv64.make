# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64)
# LastCheck: 20240502
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa, on RISC-V 64
# Copyright: Dosconio Mecocoa, BCD License Version 3

# ! current path still be same with main `Makefile`

.PHONY: ciallo lib sub new newx debug dbgend clean

uincpath=/mnt/hgfs/unisym/inc
ulibpath=/mnt/hgfs/unisym/lib
ubinpath=/mnt/hgfs/SVGN/_bin

ARCH = riscv64
THIS := $(ARCH)
OUTD = /mnt/hgfs/SVGN/_bin/mecocoa# Output Directory
OUTF = $(OUTD)/mcca-$(ARCH)# Output File
objdir=~/_obj

KitPrefix = $(ARCH)-unknown-elf-# their rules
CC = $(KitPrefix)gcc# contain AS
LD = $(KitPrefix)ld
AR = $(KitPrefix)ar
OBJCOPY = $(KitPrefix)objcopy
OBJDUMP = $(KitPrefix)objdump
PYTH = python3
COPY = cp
GDB = $(KitPrefix)gdb

#20240504 do not use '~/' or makefile will say `doesn't match the target pattern`
BUILDDIR = /home/ayano/_obj/mcca
C_SRCS = $(wildcard $(THIS)/*.c)
AS_SRCS = $(wildcard $(THIS)/*.S)
LIB_C_SRCS = $(wildcard userkit/lib/*.c)
APP_C_SRCS = $(wildcard subapps/$(ARCH)/*.c)
C_OBJS = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(C_SRCS))))
AS_OBJS = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(basename $(AS_SRCS))))
LIB_C_OBJS = $(addprefix $(BUILDDIR)/ukit-r64/, $(addsuffix .o, $(basename $(notdir $(LIB_C_SRCS)))))
APP_C_OBJS = $(addprefix $(BUILDDIR)/$(ARCH)/elf/, $(addsuffix .app, $(basename $(notdir $(APP_C_SRCS)))))
APP_C_BINS = $(addprefix $(BUILDDIR)/$(ARCH)/app/, $(addsuffix .bin, $(basename $(notdir $(APP_C_SRCS)))))
OBJS = $(C_OBJS) $(AS_OBJS)
HEADER_DEP = $(addsuffix .d, $(basename $(C_OBJS)))

-include $(HEADER_DEP)

INCLUDEFLAG = -I$(THIS) -Iinclude -I$(uincpath)
CFLAG = -Wall -Wno-error -O -fno-omit-frame-pointer -ggdb
#CFLAG += -MD
#CFLAG += -nostdinc
CFLAG += -mcmodel=medany
CFLAG += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAG += $(INCLUDEFLAG)
CFLAG += -D_OPT_RISCV64 -D_MCCA="_OPT_RISCV64" -D__BITS__=64 -D_USE_VT100 -D_DEBUG
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
CRT = $(basename $(notdir $(<)))

$(AS_OBJS): $(BUILDDIR)/$(THIS)/%.o : $(THIS)/%.S
	@mkdir -p $(@D)
	@echo "Compile: $<"
	@$(CC) $(CFLAG) -c $< -o $@

$(C_OBJS): $(BUILDDIR)/$(THIS)/%.o : $(THIS)/%.c  $(BUILDDIR)/$(THIS)/%.d
	@mkdir -p $(@D)
	@echo "Compile: $<"
	@$(CC) $(CFLAG) -c $< -o $@

$(LIB_C_OBJS): $(BUILDDIR)/ukit-r64/%.o : userkit/lib/%.c
	@mkdir -p $(@D)
	@echo "Makelib: $<"
	@$(CC) $(CFLAG) -c $< -o $@

$(APP_C_OBJS): $(BUILDDIR)/$(THIS)/elf/%.app : subapps/$(ARCH)/%.c
	@mkdir -p $(@D)
	@echo "Subapps: $(CRT)"
	@$(CC) -Wall -nostdinc -fno-omit-frame-pointer -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax $(INCLUDEFLAG) -D_OPT_RISCV64 -D_MCCA="_OPT_RISCV64" $<  -o  $(BUILDDIR)/$(THIS)/elf/$(CRT).app -L$(BUILDDIR) -luk$(ARCH) -Tuserkit/uk-$(ARCH).ld
	@$(OBJCOPY) $(BUILDDIR)/$(THIS)/elf/$(CRT).app --strip-all -O binary $(BUILDDIR)/$(THIS)/app/$(CRT).bin

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
LIBS = $(BUILDDIR)/$(THIS)/inn_*.o

ciallo:
	@echo "Mecocoa Risc-V64"
	@echo "Lglevel: $(LOG)"

lib: $(LIB_C_OBJS)
	@-rm  -f   $(BUILDDIR)/libuk$(ARCH).a
	@echo 'Build  : User libraries for $(ARCH)'
	@mkdir -p $(objdir)/mcca/$(ARCH)/elf
	@mkdir -p $(objdir)/mcca/$(ARCH)/app
	@$(CC) -c userkit/uk-$(ARCH).S -o $(BUILDDIR)/ukit-r64/_uk_.o
	@$(AR) rcs $(BUILDDIR)/libuk$(ARCH).a $(BUILDDIR)/ukit-r64/*.o
	@echo 'Build  : Kernel libraries for $(ARCH)'
	@$(CC) $(CFLAG) -c $(ulibpath)/c/mcore.c -o $(BUILDDIR)/$(THIS)/inn_microcore.o
	@$(CC) $(CFLAG) -c $(ulibpath)/c/consio.c -o $(BUILDDIR)/$(THIS)/inn_consio.o
	@$(CC) $(CFLAG) -c $(ulibpath)/c/debug.c -o $(BUILDDIR)/$(THIS)/inn_debug.o
	@$(CC) $(CFLAG) -c $(ulibpath)/c/console/conformat.c -o $(BUILDDIR)/$(THIS)/inn_conformat.o
sub: $(APP_C_OBJS)
	@mkdir -p $(objdir)/mcca/riscv64
	@python3 makefil/riscv64-link.py
	@python3 makefil/riscv64-pack.py
new: ciallo lib sub $(OBJS)
	@echo 'Link   : $(OUTF)'
	@$(LD) $(LDFLAG) -T $(objdir)/mcca/$(THIS)/kernel.ld -o $(OUTF) $(OBJS) $(LIBS)
	@$(OBJDUMP) -S $(OUTF) > $(OUTF).asm
	@$(OBJDUMP) -t $(OUTF) | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(OUTF).sym
	@echo 'Build  : Finish Mecocoa $(ARCH)'

newx: new $(OUTF)
	$(QEMU) $(QEMUOPTS)

QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::15234"; \
	else echo "-s -p 15234"; fi)

debug: new configs/riscv64.gdbinit
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB) &
	sleep 1
	$(GDB) --init-command=configs/riscv64.gdbinit

dbgend:
	sudo lsof -i tcp:15234 # sudo kill -9 46080

clean:
	rm -rf $(BUILDDIR)/$(ARCH)




