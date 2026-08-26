---
dg-publish: true
her-note: false
---

# Driver

## x86/x64

- Bus: **PCI**
- CRT Video (by BIOS)
- Interrupt 8259 **PIC** and **(x2)APIC**
- **KBD** Keyboard and **MOU** PS/2-Mouse
- COMn UART
- **KBD** USB-Keyboard and **MOU** USB-Mouse
- USB
	- xHCI 1.1/2.0/3.0 (QEMU)

TODO:
- USB
	- EHCI 1.0/2.0
	- OHCI 1.0
	- UHCI 1.0
	- xHCI 3.1

### Video

- **Buzzer** PC Speaker
- SB16

### Video

display subsystem: ... -> VTTY/VScreen -> COM/CLI/GUI
├── classic-video
│   ├── UEFI GOP framebuffer
│   └── BIOS VBE linear framebuffer
│
├── bochs-dispi
│   ├── QEMU stdvga
│   ├── QEMU bochs-display
│   ├── Bochs VGA
├── vmware-svga
│   ├── VMware SVGA-II
│   └── VirtualBox VMSVGA


TODO
│
├── virtualbox-vga
│   ├── VBoxVGA
│   └── VBoxSVGA
│
└── future
    ├── virtio-gpu
    ├── QXL
    ├── Intel/AMD/NVIDIA
    └── simple-framebuffer / ACPI-described framebuffer

No Use
├── qemu-ramfb
│   └── fw_cfg configured framebuffer

### Timer

- 8254 **PIT**
- **RTC**
- (x64u only) LAPIC Timer and ACPI PM Timer


### Storage

> ATAPI is CD

- Floppy
- PATA Disk (MBR, GPT, ATAPI) 2:2
- SATA 
	- AHCI (MBR, GPT, ATAPI) 4:30
- SCSI (MBR, GPT, ATAPI) 4:16
- NVMe (MBR, GPT) 4:64

Filesys
- FAT 12/16/32

TODO:
- SAS(including SATA)

### Network

- E1000


## RV qemuvirt

- 【!TREE】UART

