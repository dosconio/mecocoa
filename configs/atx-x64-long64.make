# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(atx-x64-uefi64){Arch(AMD64), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

arch?=atx-x64-long64

mnts=/mnt/floppy

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = #riscv64-unknown-elf-
CFLAGS += -z norelro -nostdlib -fno-builtin
CFLAGS += --static -mno-red-zone -m64  -mno-sse -mno-sse2 -mno-avx -mno-avx2 
CFLAGS += -I$(uincpath) -D_MCCA=0x8664 -D_HIS_IMPLEMENT -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -Wextra -Wno-multichar
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2 -std=c++2a
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
QARCH  = x86_64
QEMU   = qemu-system-$(QARCH)
QBOARD = atx

#
LDFILE  = prehost/$(arch)/$(arch).ld
LDFLAGS = -T $(LDFILE) 

asmpref=_ae_
cplpref=_cc_
cpppref=_cx_
dest_obj=$(uobjpath)/mcca-$(arch)
define asm_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.asm=.o)): $(1)
endef
define c_to_o
$(dest_obj)/$(cplpref)$(notdir $(1:.c=.o)): $(1)
endef
define cpp_to_o
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.o)): $(1)
endef

#
asmfile=prehost/atx-x64-uefi64/atx-x64.asm\
	$(ulibpath)/asm/x64/cpuid.asm \
	$(ulibpath)/asm/x64/sysman.asm \
	$(ulibpath)/asm/x64/inst/ioport.asm \
	$(ulibpath)/asm/x64/inst/manage.asm \
	$(ulibpath)/asm/x64/inst/interrupt.asm \
	$(ulibpath)/asm/x64/interrupt/ruptable.asm \

cppfile=$(wildcard mecocoa/*.cpp)\
	$(ulibpath)/cpp/color.cpp \
	$(ulibpath)/cpp/consio.cpp \
	$(ulibpath)/cpp/stream.cpp \
	$(ulibpath)/cpp/string.cpp \
	$(ulibpath)/cpp/interrupt.cpp \
	$(ulibpath)/cpp/lango/lango-cpp.cpp \
	$(ulibpath)/cpp/grp-base/bstring.cpp \
	$(ulibpath)/cpp/dat-block/bmmemoman.cpp \
	$(ulibpath)/cpp/dat-block/mempool.cpp \
	$(ulibpath)/cpp/system/paging.cpp \
	$(ulibpath)/cpp/Witch/Form.cpp \
	$(ulibpath)/cpp/Device/Buzzer.cpp \
	$(ulibpath)/cpp/Device/Video.cpp $(ulibpath)/cpp/Device/Video-VideoConsole.cpp \


cplfile=$(ulibpath)/c/mcore.c\
	$(ulibpath)/c/debug.c \
	$(ulibpath)/c/console/conformat.c \
	$(ulibpath)/c/driver/keyboard.c \
	$(ulibpath)/c/data/font/font-8x5.c \
	$(ulibpath)/c/data/font/font-16x8.c \
	$(ulibpath)/c/ustring/astring/StrHeap.c \
	$(ulibpath)/c/ustring/astring/salc.c \



asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %asm,%o,$(notdir $(asmfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))
elf_kernel=AMD64/mecocoa/mcca-$(arch).elf
bin_ladder=AMD64/mecocoa/ladder

mntdir=/mnt/mcca-$(arch)
sudokey=k

.PHONY : build
build: clean $(asmobjs) $(cppobjs) $(cplobjs)
	@echo MK $(arch) ladder
	aasm -o $(ubinpath)/$(bin_ladder) prehost/atx-x86-flap32/atx-ladder.asm -Iinclude/ -D_MCCA=0x8664
	@echo MK $(elf_kernel)
	$(CX) $(XFLAGS) $(LDFLAGS) \
		-o $(ubinpath)/$(elf_kernel) \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map \
		prehost/$(arch)/$(arch).cpp \
		$(uobjpath)/mcca-$(arch)/*.o
#
	@echo $(sudokey) | sudo -S kpartx -av $(ubinpath)/fixed1.vhd  >/dev/null # ls /dev/mapper/loop*p* && sudo mkfs.vfat -F 32 -n "DATA" /dev/mapper/loop*p7
	@echo $(sudokey) | sudo -S mount /dev/mapper/loop*p7 $(mnts) #sudo fsck.vfat -v /dev/mapper/loop0p7 # fdisk # blkid
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(elf_kernel)     $(mnts)/mx64.elf
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(bin_ladder)     $(mnts)/ladder
	@echo $(sudokey) | sudo -S umount $(mnts)
	@echo $(sudokey) | sudo -S kpartx -dv $(ubinpath)/fixed1.vhd >/dev/null
#

iden=mccax86.img
outs=$(ubinpath)/I686/mecocoa/$(iden)
.PHONY : run run-only
run: build run-only
run-only:
	$(QEMU) \
		-drive format=raw,file=$(outs),if=floppy \
		-boot order=a -m 256\
		-drive file=$(ubinpath)/fixed2.vhd,format=vpc,if=none,id=disk0 \
		-device ide-hd,drive=disk0,bus=ide.0,unit=0 \
		-drive file=$(ubinpath)/fixed1.vhd,format=vpc,if=none,id=disk1 \
		-device ide-hd,drive=disk1,bus=ide.0,unit=1 \
		-audiodev pa,id=speaker -machine pcspk-audiodev=speaker \
		-enable-kvm -cpu host || $(QEMU) \
		-drive format=raw,file=$(outs),if=floppy \
		-boot order=a -m 256\
		-drive file=$(ubinpath)/fixed2.vhd,format=vpc,if=none,id=disk0 \
		-device ide-hd,drive=disk0,bus=ide.0,unit=0 \
		-drive file=$(ubinpath)/fixed1.vhd,format=vpc,if=none,id=disk1 \
		-device ide-hd,drive=disk1,bus=ide.0,unit=1 \
		-audiodev dsound,id=speaker -machine pcspk-audiodev=speaker

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
# 	${RM} $(uobjpath)/mcca-$(arch)/*
	${RM} $(ubinpath)/$(arch).img
	@${MKDIR} $(uobjpath)/mcca-$(arch)


$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))

_ae_%.o:
	echo AS $(notdir $<)
	aasm -f elf64      -o $@ $< -D_MCCA=0x8664 -MD $(patsubst %.o,%.d,$@)

_cc_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(dest_obj)/*.d
