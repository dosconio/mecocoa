# Mecocoa

- type: Operating System

- domain: mecocoa.org

- alias: Kasa-x86

- license: BSD-3-Clause license



## Component Kernel32



### Variables



| Identification | Initialization | Description |
| -------------- | -------------- | ----------- |
|                |                |             |
|                |                |             |
|                |                |             |
|                |                |             |
|                |                |             |
|                |                |             |
|                |                |             |





## Memory Map

### Real-16 mode

00000~003FF IVT

00400~004FF BIOS DATA

00500~005FF COMMON BUF

00600~007FF BUFFER FOR *FAT INDEX* 



00800~009FF EXTEND BUFFER FOR *FAT INDEX* 

00A00~00BFF EXTEND STACK

00C00~00FFF REAL STACK (1 KB)



01000~07BFF 16 KERNEL (27 KB) from Disk Sec 01

07C00~9FFFF 16 SHELL

31.5K(7E00h)~640K RAM

\* At the beginning of kernel16 is a memory size check. The size of SHELL16 plus 7C00 mustn’t be less than total memory size.



### Protect-32 mode

| Fizik Address (4M at least) | Paging Size (Total 1024) | Logic Address     | Detail                     |
| --------------------------- | ------------------------ | ----------------- | -------------------------- |
| 00000000~000003FF           | 00                       |                   | IVT Page (Realors)         |
|                             |                          |                   | ...                        |
| 00000800~00000FFF           | 01                       |                   | IVT Page (Selectors)       |
| 00001000~00001FFF           | 01 (1K)                  | 80001000~80001FFF | Kernel Leader              |
| 00002000~00004FFF           | 03 (12K)                 | 80002000~80004FFF | Kernel-16b→32b             |
| 00005000~00005FFF           | 01                       | FFFFF000~FFFFFFFF | PDT                        |
| 00006000~00006FFF           | 01                       | 80006000~80006FFF | Stack                      |
| 00007000~0000707F           | (128B)                   | 80007000~8000707F | Kernel TSS                 |
| 00007080~00007FFF           | 01 (496 GDTE)            | 80007080~80007FFF | GDT                        |
|                             |                          |                   |                            |
| 00008000~00008FFF           | 01                       | 80008000~80008FFF | PT for 0x80000000          |
| 00009000~00009FFF           | 01                       | 80009000~80009FFF | PT for 0x00000000 for APP  |
| 0000A000~0007FFFF           | 118                      | 80009000~8007FFFF | **free** Kernel Area       |
| 00080000~0009FFFF           | 32                       | 80080000~8009FFFF | Page Allocating Bitmap     |
| ***Upper Reflect Area***    |                          |                   |                            |
| 000A0000~000BFFFF           | 96                       |                   | Video Display Memory       |
| 000C0000~000C7FFF           | ↑                        |                   | Video BIOS                 |
| 000C8000~000EFFFF           | ↑                        |                   | BIOS Expansions            |
| 000F0000~000FFFFF           | ↑                        |                   | Mainboard BIOS             |
| *000B8000~000BFFFF*         |                          | 800B8000~800B9000 | Video Display Buffer 32K   |
| ***User Area***             |                          |                   |                            |
| 00100000~00400000           | 768                      | 00000000~00300000 | **free** User Area         |
| *00100000~0010007F*         | (128B)                   | 00000000~00000080 | ArnHeaderCompatibleWithTSS |
|                             |                          |                   | ...                        |



#### Bitmap

To register in GPDT and allocate room.

```
00000000 FF 03 00 00-00 00 00 00=00 00 00 00-00 00 00 00
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



## Segment Map

### Global

| Segment (Hex)      | Ring | Description                      |
| ------------------ | ---- | -------------------------------- |
| 00 - 7080          | /    | *null*                           |
| 01 - 7088          | 0    | 4G R0 DATA                       |
| 02 - 7090          | 0    | Kernel 32 CODE                   |
| 03 - 7098          | 0    | 4K STACK                         |
| 04 - 70A0          | 0\*  | B8000 VIDEO                      |
| 05 - 70A8          | 0    | 32-Protect Routine               |
| 06 - 70B0          | 3    | 32-Protect Call Gate             |
| 07 - 70B8          | 0    | Kernel (Loaded by ENTRY-MBR) TSS |
| 08 - 70C0          | /    | *kept*                           |
| 09 - 70C8          | /    | *kept*                           |
| 0A - 70D0          | /    | *kept*                           |
| 0B - 70D8          | /    | *kept*                           |
| 0C - 70E0          | /    | *kept*                           |
| 0D - 70E8          | /    | *kept*                           |
| 0E - 70F0          | /    | *kept*                           |
| 0F - 70F8          | /    | *kept*                           |
| 10 - 7100          | 3    | Sub-app 0 LDT                    |
| 11 - 7108          | 3    | Sub-app 0 TSS                    |
| 12 - 7110          | 3    | Sub-app 1 LDT                    |
| xx - 7100+8+*n*×10 | 3    | ... Sub-app *n* TSS              |

### Local

| Segment (Hex) | Ring | Description |
| ------------- | ---- | ----------- |
| 00            | 3    | Header      |
| 01            | 3    | Code        |
| 02            | 3    | Data        |
| 03            | 3    | Ronl        |
| 04            | 3    | SS0 4k      |
| 05            | 3    | SS1 4k      |
| 06            | 3    | SS2 4k      |
| 07            | 3    | SS3 4nk     |





## Program Format

`0xCF` is related to her-date. 

| **Sub-program header**                          | **After load**               |
| ----------------------------------------------- | ---------------------------- |
| [ 2 byte ] – 0xCF,0x16(8086) or  0xCF,0x32(x86) | SIGPROC() to call far        |
| [ 2 byte ] – LOAD SIZE (Limit 32MB )            | SIGPROC()                    |
| [ 2 byte ] – SIGPROC() ADDRH/S                  | Code Segm                    |
| [ 2  byte ] – SIGPROC() ADDRL/A                 | PROCESS  ID                  |
| [ 2  byte ] – NUMOF SYS-API NEEDED              | PROCESS  ID (up to 0xFFFF)   |
| [ 2  byte ] – *KEPT*                            | *KEPT*                       |
| [ 4  byte ] – ENTRY() ADDR OF .CODE32/ALL16     | *KEPT*                       |
| <del>[8n  byte ] – SYS-API table</del>          | <del>SYS-API  table(#)</del> |
| [ 4  byte ] – Code addr                         | Code  Segm                   |
| [ 4  byte ] – Code leng                         | *KEPT*                       |
| [ 4  byte ] – Stak addr                         | Stak Segm                    |
| [ 4  byte ] – Stak leng                         | *KEPT*                       |
| [ 4  byte ] – Data addr                         | Data  Segm                   |
| [ 4  byte ] – Data leng                         | *KEPT*                       |



|                |             |                    |      |            |            | *164(Dec) |
| -------------- | ----------- | ------------------ | ---- | ---------- | ---------- | --------- |
|                | LDTE:S3     |                    |      |            |            | *160      |
|                |             |                    |      |            |            | *156      |
|                | LDTE:S2     |                    |      |            |            | *152      |
|                |             |                    |      |            |            | *148      |
|                | LDTE:S1     |                    |      |            |            | *144      |
|                |             |                    |      |            |            | *140      |
|                | LDTE:S0     |                    |      |            |            | *136      |
|                |             |                    |      |            |            | *132      |
|                | LDTE:Ronl   |                    |      |            |            | *128      |
|                |             |                    |      |            |            | 124       |
|                | LDTE:Data   |                    |      |            |            | 120       |
|                |             |                    |      |            |            | 116       |
|                | LDTE:Code   |                    |      |            |            | 112       |
|                |             |                    |      |            |            | 108       |
|                | LDTE:Header |                    |      |            |            | 104       |
| IOMap:103      | STRC[15,T]  |                    |      | 103        | STRC[15,T] | 100       |
| #LDTLen        | LDTSel      |                    |      |            | 0          | 96        |
|                | GS:RONL     | ConstStart         | ←    | 0          | GS         | 92        |
|                | FS:ROUT3    | ConstLen(0:!Exist) | ←    |            | FS         | 88        |
|                | DS:DATA     | DataStart          | ←    |            | DS         | 84        |
|                | SS:STAK3    | DataLen(0:!Exist)  | ←    |            | SS         | 80        |
|                | CS:CODE     | CodeStart          | ←    |            | CS         | 76        |
|                | ES:HEADER   | CodeLen>0          | ←    |            | ES         | 72        |
| EDI            | ←           |                    |      | EDI        | ←          | 68        |
| ESI            | ←           |                    |      | ESI        | ←          | 64        |
| EBP            | ←           |                    |      | EBP        | ←          | 60        |
| ESP            | ←           | StakLen            | ←    | ESP        | ←          | 56        |
| EBX            | ←           |                    |      | EBX        | ←          | 52        |
| EDX            | ←           |                    |      | EDX        | ←          | 48        |
| ECX            | ←           |                    |      | ECX        | ←          | 44        |
| EAX            | ←           |                    |      | EAX        | ←          | 40        |
| EFLAGS         | ←           |                    |      | EFLAGS     | ←          | 36        |
| EIP            | ←           | Entry=EIP          | ←    | EIP        | ←          | 32        |
| PDBR           | ←           |                    |      | PDBR       | ←          | 28        |
| AdrPointerH    | SS2         |                    |      |            | SS2        | 24        |
| ESP2           | ←           |                    |      | ESP2       | ←          | 20        |
| AdrPointerL    | SS1         |                    |      |            | SS1        | 16        |
| ESP1           | ←           |                    |      | ESP1       | ←          | 12        |
| **TIMES**      | SS0         | 0x32DE             | Ring |            | SS0        | 8         |
| ESP0           | ←(Low P of) |                    |      | ESP0       | ←          | 4         |
| CrtSubapp      | LastTSS     | LenToLoad          | ←    |            | 0          | 0         |
| **LOADED:256** | High←Low    | **TOLOAD**         | ←    | **KERNEL** | ←          | *Offset*  |



**TIMES**: even for available and odd for busy. the number is added by 2 each executed.

If length of a Code/Data segment is greater than 0x1_00000, the real length will be the times of 4K.



## Interrupts for Real-16



Note

- The **function identification** is stored by register `AH`.



80H Usual Interrupts

- 00H Print string of ASCIZ, without changing colors

    - INP DS:SI -> address of the string

    - OUT *None*



## Routines for Protect-32

Segment `8*05` 



| Identification        | Function         | IO                          |
| --------------------- | ---------------- | --------------------------- |
| 00 R_Terminate        | Terminate back   |                             |
| 01 R_Print            | Print String     | DS:ESI → ASCIZ string       |
| 02 R_Malloc           |                  |                             |
| 03 R_Mfree            |                  |                             |
| 04 R_DiskReadLBA28    |                  |                             |
| 05 R_PrintDwordCursor | PrintDwordCursor | Show the hexadecimal of EDX |
|                       |                  |                             |



## Source Files

(defaultly at least **Intel-386**)

- `BOOT.a` bootstrap, which is different from the bootstrap collected in UNISYM. 
- `Kernel.asm` 16-bit program, set up the environment and routines for 32-bit protected system. 
- `Kernel32.asm` 
- `Shell32.asm` console shell in protect-32 mode. This is going to combinated with *COTLAB*.
- `hello.asm` a demonstration sub-program which will be created by `Shell32.asm` .











