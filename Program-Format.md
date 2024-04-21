---
dg-publish: true
her-note: false
---

The current version, please to see in `unisym/*/task.h` .

This is the old version.

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

