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

| Fizik Address (4M at least) | Paging Size | Logic Address     | Detail                     |
| --------------------------- | ----------- | ----------------- | -------------------------- |
| 00000000~00000AAA           | 01          |                   | IVT                        |
| 00001000~00001FFF           | 01 4k       | 80001000~80001FFF | Kernel Leader              |
| 00002000~00004FFF           | 03 12k      | 80002000~80004FFF | Kernel-16b→32b             |
| 00005000~00005FFF           | 01 01P      | FFFFF000~FFFFFFFF | GlobalPageDirectoryTable   |
| 00006000~00006FFF           | 01          | 80006000~80006FFF | Stack                      |
| 00007000~0000707F           | 00 128      | 80007000~8000707F | Kernel TSS                 |
| 00007080~00007FFF           | 01 8×496    | 80007080~80007FFF | GDT                        |
| 00008000~00008FFF           | 01 1P       | 80008000~80008FFF | PT for 0x80000000          |
| 00009000~00009FFF           | 01 1P       | 80009000~80009FFF | PT for 0x00000000 for APP  |
| 0000A000~0007FFFF           | 79          | 80009000~8007FFFF | Kernel Area                |
| 00080000~0009FFFF           | 20 128K     | 80080000~8009FFFF | Page Allocating Bitmap     |
| 000A0000~000FFFFF           | 60          |                   | *BIOS Reflect*             |
| *000B8000~000B9000*         | 01 1P       | 800B8000~800B9000 | Video Display Buffer 80*25 |
| 00100000~00400000           | 300 3M      | 00000000~00300000 | User Area                  |
| *00100000~00100080*         | 00 128      | 00000000~00000080 | ArnHeaderCompatibleWithTSS |
|                             |             |                   |                            |



#### Bitmap



```
FF C0 00 00-00 00 00 00=00 00 00 00-00 00 00 00
FF FF FF FF-FF FF FF FF=FF FF FF FF-FF FF FF FF
00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
FF FF FF FF...
```





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











