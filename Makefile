asm=aasm
vhd=E:\vhd.vhd
vmbox=E:\software\vmbox\VBoxManage.exe
vmname=Kasa

run: ../_obj/boot.bin
	vdkcpy ../_obj/boot.bin $(vhd) 0
	$(vmbox) startvm $(vmname)
../_obj/boot.bin: BOOT.a
	$(asm) BOOT.a -o ../_obj/boot.bin -I../unisym/Kasha/n_
