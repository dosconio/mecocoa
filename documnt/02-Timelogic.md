---
dg-publish: true
her-note: false
---

Since 20250102:

```
[qemuvirt-r32/64]
VIRT -> rv-kernel

[atx-x64-uefi64]
UEFI -> x64-kernel

[atx-x86-flap32]
BIOS -> uni.bootx86(floppy) -> x86-loader(floppy) -> x86-kernel(hdisk0:1)

Grub -----------> mx86-kernel
Unib -> loader -> mx86-kernel

```

Since 20241111 (OLD UNFINISHED):

```
x86 (IBM)-> boot-x86 -> successor(real-16) {
	
}
main(flap-32) { 
	... 
}

r64 ...
```


