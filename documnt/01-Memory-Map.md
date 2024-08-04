---
dg-publish: true
her-note: false
---


**Kasha Memline 2024AD**



| Fizik Address                         | Pages         | Type                  | Detail 32                                          |
| ------------------------------------- | ------------- | --------------------- | -------------------------------------------------- |
| 00000000~000003FF                     | 00            | fixed                 | IVT Page (Realors)                                 |
| 00000400~000004FF                     | 00            | fixed                 | BIOS Data Area                                     |
| 00000500~000005FF                     | 00            | fixed                 | KASHA Data Area                                    |
| 00000600~000007FF                     | 00            | fixed                 | Kernel Stack (DF=1)                                |
| 00000800~00000FFF                     | 01            | TDM                   | `R16` Kernel Buffer <br>`P32` IVT Page (Selectors) |
| ***Kernel Area***                     |               |                       |                                                    |
| 00001000~00001FFF                     | 01            | resident since kernel | PT for High Reflect (00000000~003FFFFF)            |
| 00002000~00002FFF                     | 01            | resident              | PDT for Kernel                                     |
| 00003000~00003FFF                     | 01            | fixed                 | Stack                                              |
| 00004000~0000407F                     | 00            | resident              | TSS for Shell Real-16                              |
| 00004080~000040FF                     | 00            | resident              | TSS for Shell Prot-32                              |
| 00004100~00004FFF                     | 01 (480 GDTE) | resident              | GDT                                                |
| 00005000~0000FFFF                     | 11            | resident              | Kernel (Real-16 + Flap-32)                         |
| >00005000~000057FF                    |               |                       | Kernel.Data                                        |
| >00005800~00007BFF                    |               |                       | Kernel.Code                                        |
| >00008000~00009FFF                    |               |                       | Shell16.Data                                       |
| >0000A000~0000BFFF                    |               |                       | Shell16.Code                                       |
| >0000C000~0000FFFF                    |               |                       | Rest Kernel Area                                   |
| 00010000~0005FFFF                     | 80            | active                | **Least User Area**                                |
| 00060000~0007FFFF                     | 32            | fixed                 | Page Allocating Bitmap                             |
| 00080000~0009FFFF                     | 32            | fixed                 | Extended BIOS Data Area                            |
| ***Upper Reflect Area*** (Over 640KB) |               |                       |                                                    |
| 000A0000~000BFFFF                     | 96            | fixed                 | Video Display Memory                               |
| 000C0000~000C7FFF                     | ↑             | fixed                 | Video BIOS                                         |
| 000C8000~000EFFFF                     | ↑             | fixed                 | BIOS Expansions                                    |
| 000F0000~000FFFFF                     | ↑             | fixed                 | Mainboard BIOS                                     |
| *000B8000~000BFFFF*                   |               | fixed                 | Video Display Buffer 32K                           |
| ***User Area***                       |               |                       |                                                    |
| 00100000~00400000                     | 768           |                       | Extend-32 User Area                                |
|                                       |               |                       | ...                                                |

- **TDM** abbreviation of time-division multiplexing.
- **Least Phyzical Memory** 4M.
- **Total Size of Pages** 1024.
- **High Reflect** Linear Address = Logical Address - 0x80000000 (Logical Address >= 0x80000000)
- Kernel will always give IP to shell after finishing its work.

**Special** 

- FFC00000~FFFFEFFF → PTs
- FFFFF000~FFFFFFFF → 00005000~00005FFF (PDT)

**Detail**

{TEMP}

10000 -> 11FFF Buffer

12000 Shell32 Raw File

- Shell32 Data: 0x21000 -> 0x22000
- Shell32 Code: 0x22000 -> 0x24000
- SubappA Data: 0x24000 -> 0x25000
- SubappA Code: 0x25000 -> 0x26000
- SubappB Data: 0x26000 -> 0x27000
- SubappB Code: 0x27000 -> 0x28000
- SubappC Data: 0x28000 -> 0x29000
- SubappC Code: 0x29000 -> 0x2A000

0x50000 TEMP PT (0x40'0000)


//{TODO} combinate TSS and LDT and others into "Task Block"
//{TEMP} TSS and LDT 0x2xxxx
//{TEMP} SubappABC Raw: 0x30000 -> 


### Memory Bitmap

To register in GPDT and allocate room.

```
00000000 FF 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00080000 FF FF FF FF-FF FF FF FF=FF FF FF FF-FF FF FF FF
00100000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00180000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00200000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00280000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00300000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00380000 00 00 00 00-00 00 00 00=00 00 00 00-00 00 00 00
00400000 FF FF FF FF... 
```

A digit stands 4×4k=16k(0x4000);

A line stands 16k×2×16=512k=pow2(19)=0x80000;



### Variables


Address: 0x500 ~ 0x5FF


| Offset   | Object                         | Initialization | Description                   |
| -------- | ------------------------------ | -------------- | ----------------------------- |
| 00 word  | IP of Prot-32 Entry            |                |                               |
| 02 word  | CS of Prot-32 Entry            |                |                               |
| 04 word  | GDT32 Length                   |                |                               |
| 06 dword | GDT32 Address                  |                |                               |
| 0A word  | IVT32 Length                   |                |                               |
| 0C dword | IVT32 Address                  |                |                               |
| 10 qword | zero                           |                |                               |
| 18 dword | CurrentScreenMode              |                |                               |
| 1C dword | ConsoleForeColor (BackColor)   |                |                               |
| 20 dowrd | ConsoleDefaultColor (PenColor) |                |                               |
| 24 dword | Count : Rotate Seconds         |                | 0->FFFFFFFF->0->...           |
| 28 word  | Count: Rotate Millisecond      |                |                               |
| 2A word  | Simple Kernel Memory Pointer   | 0x8000         | 0x0000 for full               |
| 2C dword | Simple User Memory Pointer     | 0x00010000     | 0x00000000 for full           |
| 30 dword | Global Keyword Queue           |                | putptr+getptr+data            |
| 34 dword | ReadyFlag1                     |                | MSB `31:SwitchTaskReady,` LSB |
| 38 dword | TasksAvailableSelectorsPointer |                | First One is the numbers      |

### Global Segment

| Segment (Hex)      | Ring | Description                      |
| ------------------ | ---- | -------------------------------- |
| 00 - 7080          | /    | *null*                           |
| 01 - 7088          | 0    | 4G R0 32 DATA and STAK           |
| 02 - 7090          | 0    | 4G R0 32 CODE                    |
| 03 - 7098          | 0    | <del>4K STACK</del> but not use  |
| 04 - 70A0          | 0\*  | B8000 VIDEO                      |
| 05 - 70A8          | 0    | <del>32-Protect Routine</del>    |
| 06 - 70B0          | 3    | 32-Protect Call Gate             |
| 07 - 70B8          | 0    | Kernel (Loaded by ENTRY-MBR) TSS |
| 08 - 70C0          | /    | *todo*Global-Data-R1                           |
| 09 - 70C8          | /    | *todo*Global-Data-R2                           |
| 0A - 70D0          | /    | *todo*Global-Data-R3                           |
| 0B - 70D8          | /    | *todo*Global-Code-R3                           |
| 0C - 70E0          | /    | *kept*                           |
| 0D - 70E8          | /    | *kept*                           |
| 0E - 70F0          | /    | *kept*                           |
| 0F - 70F8          | /    | *kept*                           |
| 10 - 7100          | 3    | Sub-app 0 LDT                    |
| 11 - 7108          | 3    | Sub-app 0 TSS                    |
| 12 - 7110          | 3    | Sub-app 1 LDT                    |
| xx - 7100+8+*n*×10 | 3    | ... Sub-app *n* TSS              |

### Local Segment

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


