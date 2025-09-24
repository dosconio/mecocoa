---
dg-publish: true
dg-home: true
her-note: false
---

# Mecocoa ![LOGO](./rsource/logo/MCCA20240501.png) 

- **type**: Operating System
- **domain**: [mecocoa.org](http://mecocoa.org/) 
- **repository** : [GitHub](https://github.com/dosconio/mecocoa)  @dosconio
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

## Architecture

Format `board-architecture-mode`


CISC `VMBox/VMware/Bochs/QEMU/TODO(Simics,Wel)`
- `x86` Intel x86 (8086 -> i686+)
	- BIOS(MBR), CLI, Paging, Multitask, Syscall, {RTC,PIT,KBD}
	- dev-env `[native x64+multilib]* (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0`
	- **run**
		- **virtual**: VMware, VMBox, Bochs, qemu-system-i386
	- **build**
		- `make lib`; `make` `make run`
- `x64` AMD64
	- UEFI, GUI
	- dev-env `[native x64+multilib]* (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0`
	- **run**
		- **virtual**: qemu-system-x86_64
	
RISC `Fizik`
- `r32` RISC-V32
	- CLI
	- **run**
		- **virtual**: qemu-system-riscv32
	- **build**
		- `make lib-r32`; `make build-r32` `make run-r32`
- `r64` RISC-V64
- `ac7` ARMv7 Cortex-A7
	- dev-env `arm-none-eabi-* (Arm GNU Toolchain 12.2.MPACBTI-Rel1 (Build arm-12-mpacbti.34)) 12.2.1 20230214`
	- **run**
		- **phyzikl**: ...

MISC
- `m64` *kept for Dinah* 

