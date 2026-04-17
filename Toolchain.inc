
MKDIR = mkdir -p
RM = rm -rf

ifeq ($(arch),atx-x64-long64)
GARCH = x86_64
else ifeq ($(arch),qemuvirt-r32)
GARCH = riscv32
else ifeq ($(arch),qemuvirt-r64)
GARCH = riscv64
else
$(error Unsupported arch)
endif

# (GNU)
ifeq ($(arch),atx-x64-long64)
GPREF   = $(GARCH)-linux-gnu-

else ifeq ($(arch),qemuvirt-r32)
RISCV_ELF_EXISTS := $(shell command -v riscv64-elf-gcc 2>/dev/null)
RISCV_UNKNOWN_EXISTS := $(shell command -v riscv64-unknown-elf-gcc 2>/dev/null)
ifeq ($(RISCV_ELF_EXISTS),)
    GPREF := riscv64-unknown-elf-
else
    GPREF := riscv64-elf-
endif

else ifeq ($(arch),qemuvirt-r64)
RISCV_ELF_EXISTS := $(shell command -v riscv64-elf-gcc 2>/dev/null)
RISCV_UNKNOWN_EXISTS := $(shell command -v riscv64-unknown-elf-gcc 2>/dev/null)
ifeq ($(RISCV_ELF_EXISTS),)
    GPREF := riscv64-unknown-elf-
else
    GPREF := riscv64-elf-
endif

endif

CFLAGS += -z norelro -nostdlib -fno-builtin
CFLAGS += --static
CFLAGS += -I$(uincpath) 
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -Wextra -Wno-multichar -Wno-unused-variable -Wno-unused-function -Wno-parentheses -Wno-comment
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2 -std=c++2a
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
QARCH  = $(GARCH)
QEMU   = qemu-system-$(QARCH)


#
LDFILE  = prehost/$(arch)/$(arch).ld
LDFLAGS = -T $(LDFILE) 
