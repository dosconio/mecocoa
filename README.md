---
dg-publish: true
dg-home: true
her-note: false
---

# Mecocoa ![LOGO](./rsource/logo/MCCA20240501.png) 

- **project**: Operating System (Distributed)
- **domains**: [mecocoa.org](http://mecocoa.org/) 
- **feature**: Distributed, Realtime(by SysTimer)
- **reposit** : [GitHub](https://github.com/dosconio/mecocoa)  @dosconio
- **license**: BSD-3-Clause license

**Dependence** :
- Ubuntu (Dev.Env)
- GCC and GLIBC
- QEMU, or x86 native virtual machines
- EDK2
	- git clone https://github.com/tianocore/edk2.git
	- git checkout edk2-stable202502 # edk2-stable202208
	- git submodule update --init --recursive
	- make -C /home/phina/soft/edk2/BaseTools/Source/C

**Branch**
	- `main`/master: add new features and test for UNISYM.
	- `tutorial` (TODO)

## Requirement

Hardware
- Processor
	- Interrupt
	-    Paging: Or the dynamic_loading will be disabled, macro-kernel RTOS only.

Development-Environment
- `(Stable)` Ubuntu 22.04, gcc 11.4.0, qemu 9.2.4
- `(Newest)` ArchLinux, gcc, qemu

## Architecture

Format $board-architecture(-mode)$


|MAGIC     |IDEN           |ISA  |ARCH        |CHIP       |BOARD      |
|----------|---------------|-----|------------|-----------|------------
|0x    0064|kept for Dinah |MISC |/           |/          |/          |
|0x    8664|atx-x64-long64 |amd64|amd64       |/          |aTX        |
|0x    8664|atx-x64-uefi64 |amd64|amd64       |/          |aTX        |
|0x    8632|atx-x86-flap32 |ix86 |i586+       |/          |aTX        |
|0x    2064|qemuvirt-a64   |armvN|/           |-cpu max   |QEMU-virt  |
|0x    1032|qemuvirt-r32   |riscv|/           |/          |QEMU-virt  |
|0x    1064|qemuvirt-r64   |riscv|/           |/          |QEMU-virt  |
|0x10532064|raspi3-ac53    |armv7|cortex-a53  |BCM2837    |RasPi3-B   |
|0x1A072032|stm32h743X-ac7m|armv7|cortex-m7   |STM32H743x |?          |
|0x10072032|stm32mp13X-ac7 |armv7|cortex-a7   |STM32MP13x |ATK-DLMP135|


* (MAGIC) ARM
	* Cortex-A occupy 0x100020nn~0x199920nn
	* Cortex-R is 0x1Bnn20nn
* (ISA) x86&64 modes: real16 <=> flap32 <=> long64, uefi64. No IA-64
* (board) aTX for IBM, ATX, ITX ...

Kernel Name:
- mx86.elf
- mx64.elf
- ux64.elf

## Toolchain

Build
- `arm-none-eabi-* (Arm GNU Toolchain 12.2.MPACBTI-Rel1 (Build arm-12-mpacbti.34)) 12.2.1 20230214`

Build Example:
- `x86` Intel x86/64 (i586+)
	- `make` -> mx32.elf
	- `make install -f configs/atx-x86-flap32.make` for grub bootstraps
- `x64` AMD64 (IA-32e)
	- `arch=atx-x64-long64 make` -> mx64.elf
	- `arch=atx-x64-uefi64 make` -> ux64.elf

Emulate
- VMware, VMBox, Bochs, QEMU-system-i386(9.2.4), TODO(Simics,Wel)
