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
| 00005000~0000FFFF                     | 11            | resident              | Kernel (Real-16 + Prot-32)                         |
| `====5000~====57FF`                      |               |                       | Kernel.Data                                        |
| `====5800~====7BFF`                      |               |                       | Kernel.Code                                        |
| `====8000~====9FFF`                      |               |                       | Shell16.Data                                       |
| `====A000~====FFFF`                      |               |                       | Shell16.Code                                       |
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

### Variables



| Identification | Address | Initialization | Description |
| -------------- | ------- | -------------- | ----------- |
|                |         |                |             |
|                |         |                |             |
|                |         |                |             |
|                |         |                |             |
|                |         |                |             |
|                |         |                |             |
|                |         |                |             |





### Bitmap

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



### Kasha Area



| Offset 500~5FF | Object | Description |
| ---- | ---- | ---- |
| 00 word | IP of Prot-32 Entry |  |
| 02 word | CS of Prot-32 Entry |  |
| 04 word | GDT32 Length |  |
| 06 dword | GDT32 Address |  |
| 0A word | IVT32 Length |  |
| 0C dword | IVT32 Address |  |
| 10 qword | zero |  |
| 18 dword | CurrentScreenMode |  |
| 1C dword | ConsoleForeColor (BackColor) |  |
| 20 dowrd | ConsoleDefaultColor (PenColor) |  |
| 24 dword | Count : Rotate Seconds | 0->FFFFFFFF->0->... |
|  |  |  |


