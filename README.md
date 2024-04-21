---
dg-publish: true
dg-home: true
her-note: false
---


# Mecocoa

- **type**: Operating System
- **domain**: mecocoa.org
- **repository** : [GitHub](https://github.com/dosconio/mecocoa)  @dosconio
- **alias**: Kasa-x86
- **license**: BSD-3-Clause license



## Components



| Program        | Format     | Description |
| -------------- | ---------- | ----------- |
| USYM BOOT      | FLAT A     |             |
| Kernel         | HerELF A+C |             |
| Shell16        | ELF C      |             |
| Shell32 | ELF C++    | (to COTLAB)            |
|                |            |             |




## [Memory-Map](Memory-Map.md) 


## Segment Map

### Global

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





## [Program-Format](Program-Format.md) 


## Interrupts for Real-16



Note

- The **function identification** is stored by register `AH`.



80H Usual Interrupts

- 00H Print string of ASCIZ, without changing colors

    - INP DS:SI -> address of the string

    - OUT *None*



## Routines for Protect-32

Segment `8*05` 



| Identification     | Function         | IO                          |
| ------------------ | ---------------- | --------------------------- |
| `ELSE` RotTerminal   | Terminate back   |                             |
| `00` RotPrint        | Print String     | DS:ESI → ASCIZ string       |
| `01` RotEchoDword    | PrintDwordCursor | Show the hexadecimal of EDX |
| 02 R_Malloc        |                  |                             |
| 03 R_Mfree         |                  |                             |
| 04 R_DiskReadLBA28 |                  |                             |
|                    |                  |                             |
| `10`                     |                  | EDX:EAX<<<A(Addr)B(Len)C(Prop)                            |



## Source Files

(default, at least **Intel-386**)


