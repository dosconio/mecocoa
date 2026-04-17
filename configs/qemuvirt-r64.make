# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(qemuvirt-r64){Arch(RISCV), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3


CFLAGS += -march=rv64g -mabi=lp64 -mcmodel=medany
CFLAGS += -D_MCCA=0x1064 -D_OPT_RISCV64 -D_DEBUG
include Toolchain.inc
QBOARD = virt
QFLAGS = -smp 1 -machine $(QBOARD) -bios none


#
gasfile=$(wildcard prehost/qemuvirt-r32/*.S)
cppfile=$(wildcard prehost/qemuvirt-r32/*.cpp)
include Makefile.inc
LDFLAGS = -T $(LDFILE).ignore

elf_kernel=mcca-$(arch).elf

archdir=$(ubinpath)/RISCV64/mecocoa
mntdir=/mnt/floppy
clang=clang-14
sudokey=k

.PHONY : build
build: clean prehost/$(arch)/fatvhd.ignore $(gasobjs) $(cppobjs) $(cplobjs)
	#echo [building] MCCA for $(arch)
	@echo MK $(elf_kernel)
	@perl configs/qemuvirt-riscv.pl r64 > $(LDFILE).ignore
	@${CC} -E -P -x c ${CFLAGS} $(LDFILE).ignore -D_DEV_GNU_AS -D__BITS__=64 > $(LDFILE)
	@mv $(LDFILE) $(LDFILE).ignore
	@${CC} ${XFLAGS} ${LDFLAGS} -o $(ubinpath)/${elf_kernel} $(uobjpath)/mcca-$(arch)/*.o
	# readelf -h $ubinpath/mcca-qemuvirt-r64.elf| grep Entry
	# bin_kernel : ${OBJCOPY} -O binary ${elf_kernel} ${BIN}
	@echo
	@echo Run \"make -f accmlib/accmrv64.make\" to build accm-r64

$(uobjpath)/sapp-$(arch)/loop_echo: subapps/_basic/loop_echo.cpp
	$(MKDIR) $(uobjpath)/sapp-$(arch)
	echo MK $<
	@${CX} ${XFLAGS} $< -o $(uobjpath)/sapp-$(arch)/loop_echo \
		-L$(uobjpath)/accm-riscv64 -lriscv64 -T accmlib/accmrv.ld
$(archdir)/kerdisk.fat: $(uobjpath)/sapp-$(arch)/loop_echo
	$(MKDIR) $(archdir)
	dd if=/dev/zero of=$@ bs=1M count=1
	mkfs.fat -n 'MECOCOA2' -s 2 -f 2 -R 32 -F 32 $@
	@echo $(sudokey) | sudo -S mkdir -p $(mntdir)
	@echo $(sudokey) | sudo -S mount -o loop $(archdir)/kerdisk.fat $(mntdir)
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/loop_echo $(mntdir)/lpa.elf
	tree $(mntdir)
	@echo $(sudokey) | sudo -S umount $(mntdir)
prehost/$(arch)/fatvhd.ignore: $(archdir)/kerdisk.fat
	cp $(archdir)/kerdisk.fat prehost/$(arch)/fatvhd.ignore

.PHONY : run run-only debug
run: build
	@echo [ running] MCCA for $(arch)
	@echo "( Press ^A and then X to exit QEMU )"
	${QEMU} ${QFLAGS} -nographic -kernel $(ubinpath)/${elf_kernel}
# 	@${QEMU} -M ? | grep virt >/dev/null || exit
run-only:
	${QEMU} ${QFLAGS} -kernel $(ubinpath)/${elf_kernel}


debug: build
	# sudo lsof -t -i :1234 | xargs sudo kill -9
	@${QEMU} ${QFLAGS} -nographic -kernel $(ubinpath)/${elf_kernel} -s -S &
	@${G_DBG} $(ubinpath)/${elf_kernel} -q -x configs/qemuvirt-rv.gdbinit


.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
# 	${RM} $(uobjpath)/mcca-$(arch)/*
	${RM} $(archdir)/kerdisk.fat
	@${MKDIR} $(uobjpath)/mcca-$(arch)


_ag_%.o:
	echo AS $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@ -D_DEV_GNU_AS -D__BITS__=64

_cc_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@ \
		-fno-rtti

-include $(dest_obj)/*.d



