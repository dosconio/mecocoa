# sudo apt install xorriso mtools libisoburn1
# phina@diamp:/mnt/hgfs/bin$ tree mecocoa/
# mecocoa/
# └── boot
#     ├── grub
#     │   └── grub.cfg
#     └── mcca-atx-x86-flap32.elf

rm mcca.iso
cp ./I686/mecocoa/mcca-atx-x86-flap32.elf ./mecocoa/boot/
cp ./I686/mecocoa/mcca-atx-x86-flap32.loader.elf ./mecocoa/boot/
grub-mkrescue -o mcca.iso mecocoa
echo "run x86: qemu-system-i386 -m 32M -hda mcca.iso -enable-kvm -cpu host"

