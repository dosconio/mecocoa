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
- 01H Print Integer u32 without attribute
	- INP EDX
	- OUT ...
- 02H Load File from FAT12 Floppy
	- INP 
	- OUT Loaded at DI+`0x800`
- 03H



## Routines for Flap-32

Gate Segment `8*05` 

| Identification            | Function         | IO                             |
| ------------------------- | ---------------- | ------------------------------ |
| `ELSE` RotTerminal        | Terminate back   |                                |
| `00` RotPrint             | Print String     | DS:ESI → ASCIZ string          |
| `01` RotEchoDword         | PrintDwordCursor | Show the hexadecimal of EDX    |
| `02` R_Malloc             |                  |                                |
| `03` R_Mfree              |                  |                                |
| `04` R_DiskReadLBA28      |                  |                                |
| `05`                      |                  |                                |
| `06` R_SysDelay           | Delay in ms      | ECX=ms Interrupt Dependent     |
|                           |                  |                                |
|                           |                  |                                |
| `10`                      |                  | EDX:EAX<<<A(Addr)B(Len)C(Prop) |
| `11` R_TEMP_OpenInterrupt |                  | (System Leak Point **TODO**)   |


