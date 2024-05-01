---
dg-publish: true
her-note: false
---

## Routines for Real-16

form: Interrupts

Note

- The **function identification** is stored by register `AH`.



80H Usual Interrupts

- 00H Print string of ASCIZ, without changing colors

    - INP DS:SI -> address of the string

    - OUT *None*



## Routines for Protect-32

Gate Segment `8*05` 

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


