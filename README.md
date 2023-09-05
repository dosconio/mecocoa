# Mecocoa

- type: Operating System

- domain: mecocoa.org

- alias: Kasa-x86

- license: BSD-3-Clause license



### Mecocoa's Memory map

00000~003FF IVT

00400~004FF BIOS DATA

00500~005FF COMMON BUF

00600~00FFF STACK (2.5 KB)

01000~07BFF 16 KERNEL (27 KB) from Disk Sec 01

07C00~9FFFF 16 SHELL



31.5K(7E00h)~640K RAM

\* At the beginning of kernel16 is a memory size check. The size of SHELL16 plus 7C00 mustn’t be less than total memory size.



### **Mecocoa's Program format**

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









