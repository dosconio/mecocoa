
ulib=/mnt/hgfs/unisym

all: clean
	g++ -c core.cpp -o core.obj -m32 -fleading-underscore -fno-pic
	ld core.obj -Tdata 0x7E00 -Ttext 0xA000 -o core -m elf_i386 -s
	/mnt/hgfs/_bin/ELF64/aasm $(ulib)/demo/osdev/bootstrap/bootfx.a \
		-I$(ulib)/inc/Kasha/n_ -I$(ulib)/inc/naasm/n_ -o boot -D_FLOPPY #-D_DEBUG
	dd if=/dev/zero of=easyos.img bs=512 count=2880 2>>/dev/null
	dd if=boot      of=easyos.img bs=512 count=1 conv=notrunc 2>>/dev/null
	sudo mount -o loop easyos.img /mnt/floppy/
	sudo cp core /mnt/floppy/KER.OBJ
	sudo umount /mnt/floppy/
	sudo cp easyos.img /mnt/hgfs/share/easyos.img
clean:
	-rm boot core core.obj easyos.img
