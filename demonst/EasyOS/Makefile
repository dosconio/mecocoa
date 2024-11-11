
ulib=$(ulibpath)/..
boot=$(ubinpath)/boot-x86.bin
outs=$(ubinpath)/mecocoa/easyos.img
mnts=/mnt/floppy
qemu = qemu-system-x86_64

all: a b c
a:
	-rm $(outs)
	g++ -c core.cpp -o core.obj -m32 -fleading-underscore -fno-pic
	ld core.obj -Tdata 0x7E00 -Ttext 0xA000 -o core -m elf_i386 -s
	dd if=/dev/zero of=$(outs) bs=512 count=2880 2>>/dev/null
	dd if=$(boot)   of=$(outs) bs=512 count=1 conv=notrunc 2>>/dev/null
	ls --color

b:
	sudo mount -o loop $(outs) $(mnts)
	sudo mv core $(mnts)/KEX.OBJ
c:
	sudo umount $(mnts)
	-rm core.obj
	sudo $(qemu) -drive format=raw,file=$(outs),if=floppy -boot order=a -m 4M

