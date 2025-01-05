---
dg-publish: true
her-note: false
---


| Fizik Address                         | Detail 32                |
| ------------------------------------- | ------------------------ |
| `00000000`~`000003FF`                 | Kept                     |
| `00000400`~`000004FF`                 | BIOS Data Area           |
| `00000500`~`000005FF`                 | KASHA Data Area          |
| `00000600`~`000007FF`                 | GDT                      |
| `00000800`~`00000FFF`                 | IVT Page (Selectors)     |
| ***Kernel Area***                     |                          |
| `00001000`~`00007BFF`                 | area kernel              |
| `00007C00`~`00007DFF`                 | bootstrap kept           |
| `00007E00`~`0007FFFF`                 | area basic               |
| `00080000`~`0009FFFF`                 | Extended BIOS Data Area  |
| ***Upper Reflect Area*** (Over 640KB) |                          |
| `000A0000`~`000BFFFF`                 | Video Display Memory     |
| `000C0000`~`000C7FFF`                 | Video BIOS               |
| `000C8000`~`000EFFFF`                 | BIOS Expansions          |
| `000F0000`~`000FFFFF`                 | Mainboard BIOS           |
| > `000B8000~000BFFFF`                 | Video Display Buffer 32K |
| ***User Area***                       |                          |
| 00100000~FFFFFFFF                     | area optional            |

$Least Size = area_0 + area_1 = 0x06C00 + 0x78200 = 0x7E000$

so 1M physical memory required at least.  

### Memory Bitmap

{TODO} 重新计算

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

| Offset   | Object                         | Initialization | Description              |
| -------- | ------------------------------ | -------------- | ------------------------ |
| 00 word  | IP of Prot-32 Entry            |                |                          |
| 02 word  | CS of Prot-32 Entry            |                |                          |
| 04 word  | GDT32 Length                   |                |                          |
| 06 dword | GDT32 Address                  |                |                          |
| 0A word  | IVT32 Length                   |                |                          |
| 0C dword | IVT32 Address                  |                |                          |
| 10 qword | zero                           |                |                          |
| 18 dword | CurrentScreenMode              |                |                          |
| 1C dword | ConsoleForeColor (BackColor)   |                |                          |
| 20 dowrd | ConsoleDefaultColor (PenColor) |                |                          |
| 24 dword | Count : Rotate Seconds         |                | 0->FFFFFFFF->0->...      |
| 28 dword | Count: Rotate Millisecond      |                |                          |


### Global Segment

| Segment (Hex) | Ring | Description |
| ------------- | ---- | ----------- |
| 00            | 3    | Null        |
| 01            | 3    | xData       |
| 02            | 3    | Code        |
| 03            | 3    | Rout        |



