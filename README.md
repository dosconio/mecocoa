# Mecocoa

- type: Operating System

- domain: mecocoa.org

- alias: Kasa-x86

- license: BSD-3-Clause license



## Mecocoa's Memory Map

### Real-16 mode

00000~003FF IVT

00400~004FF BIOS DATA

00500~005FF COMMON BUF

00600~00FFF STACK (2.5 KB)

01000~07BFF 16 KERNEL (27 KB) from Disk Sec 01

07C00~9FFFF 16 SHELL

31.5K(7E00h)~640K RAM

\* At the beginning of kernel16 is a memory size check. The size of SHELL16 plus 7C00 mustn’t be less than total memory size.



### Protect-32 mode

| Fizik Address (4M at least) | Paging Size (Total 1024) | Logic Address     | Detail                     |
| --------------------------- | ------------------------ | ----------------- | -------------------------- |
| 00000000~00000FFF           | 01                       |                   | IVT                        |
| 00001000~00001FFF           | 01                       | 80001000~80001FFF | Kernel Leader              |
| 00002000~00004FFF           | 03                       | 80002000~80004FFF | Kernel-16b→32b             |
| 00005000~00005FFF           | 01                       | FFFFF000~FFFFFFFF | PDT                        |
| 00006000~00006FFF           | 01                       | 80006000~80006FFF | Stack                      |
| 00007000~0000707F           | (128B)                   | 80007000~8000707F | Kernel TSS                 |
| 00007080~00007FFF           | 01                       | 80007080~80007FFF | GDT                        |
|                             |                          |                   |                            |
| 00008000~00008FFF           | 01                       | 80008000~80008FFF | PT for 0x80000000          |
| 00009000~00009FFF           | 01                       | 80009000~80009FFF | PT for 0x00000000 for APP  |
| 0000A000~0007FFFF           | 118                      | 80009000~8007FFFF | **free** Kernel Area       |
| 00080000~0009FFFF           | 32                       | 80080000~8009FFFF | Page Allocating Bitmap     |
| 000A0000~000FFFFF           | 96                       |                   | *BIOS Reflect*             |
| *000B8000~000BFFFF*         |                          | 800B8000~800B9000 | Video Display Buffer 32K   |
| 000C0000~000FFFFF           |                          |                   |                            |
| 00100000~00400000           | 768                      | 00000000~00300000 | **free** User Area         |
| *00100000~0010007F*         | (128B)                   | 00000000~00000080 | ArnHeaderCompatibleWithTSS |
|                             |                          |                   |                            |




#### Bitmap

To register in GPDT and allocate room.

```
00000000 FF C0 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00080000 FF FF FF FF-FF FF FF FF=FF FF FF FF-FF FF FF FF
00100000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00180000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00200000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00280000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00300000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00380000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00400000 FF FF FF FF... 
```

A digit stands 4×4k=16k;

A line stands 16k×2×16=512k=pow2(19)=0x80000;



## Mecocoa's Segment Map

| Segment | Ring | Description                   |
| ------- | ---- | ----------------------------- |
| 00      |      | *null*                        |
| 01      |      | 4G R0 DATA                    |
| 02      |      | Kernel 32 CODE                |
| 03      |      | 4K STACK                      |
| 04      |      | B8000 VIDEO                   |
| 05      |      | 32-Protect Routine            |
| 06      |      | 32-Protect Call Gate          |
| 07      |      | App (Loaded by ENTRY-MBR) TSS |
| 08      |      | Shell (R3 B32) TSS            |
| 09      |      | Shell (R3 B32) LDT            |
| 0A      |      |                               |
| 0B      |      |                               |
| 0C      |      |                               |



## Mecocoa's Program Format

`0xCF` is related to her-date. 

| **Sub-program header**                          | **After load**             |
| ----------------------------------------------- | -------------------------- |
| [ 2 byte ] – 0xCF,0x16(8086) or  0xCF,0x32(x86) | SIGPROC() to call far      |
| [ 2 byte ] – LOAD SIZE (Limit 32MB )            | SIGPROC()                  |
| [ 2 byte ] – SIGPROC() ADDRH/S                  | Code Segm                  |
| [ 2  byte ] – SIGPROC() ADDRL/A                 | PROCESS  ID                |
| [ 2  byte ] – NUMOF SYS-API NEEDED              | PROCESS  ID (up to 0xFFFF) |
| [ 2  byte ] – *KEPT*                            | *KEPT*                     |
| [ 4  byte ] – ENTRY() ADDR OF .CODE32/ALL16     | *KEPT*                     |
| [8n  byte ] – SYS-API table                     | SYS-API  table(#)          |
| [ 4  byte ] – Code addr                         | Code  Segm                 |
| [ 4  byte ] – Code leng                         | *KEPT*                     |
| [ 4  byte ] – Stak addr                         | Stak Segm                  |
| [ 4  byte ] – Stak leng                         | *KEPT*                     |
| [ 4  byte ] – Data addr                         | Data  Segm                 |
| [ 4  byte ] – Data leng                         | *KEPT*                     |



## Mecocoa's Interrupts for Real-16



Note

- The **function identification** is stored by register `AH`.



80H Usual Interrupts

- 00H Print string of ASCIZ, without changing colors

    - INP DS:SI -> address of the string

    - OUT *None*



## Mecocoa's Interrupts for Protect-32

Segment `8*05` 



| Identification     | Function       | IO                    |
| ------------------ | -------------- | --------------------- |
| 00 R_Terminate     | Terminate back |                       |
| 01 R_Print         | Print String   | DS:ESI → ASCIZ string |
| 02 R_Malloc        |                |                       |
| 03 R_Mfree         |                |                       |
| 04 R_DiskReadLBA28 |                |                       |
|                    |                |                       |
|                    |                |                       |



## Mecocoa's Source Files

(defaultly at least **Intel-386**)

- `BOOT.a` bootstrap, which is different from the bootstrap collected in UNISYM. 
- `Kernel.asm` 16-bit program, set up the environment and routines for 32-bit protected system. 
- `Kernel32.asm` 
- `Shell32.asm` console shell in protect-32 mode. This is going to combinated with *COTLAB*.
- `hello.asm` a demonstration sub-program which will be created by `Shell32.asm` .











