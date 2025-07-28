---
dg-publish: true
her-note: false
---

20250707のAzusa(ArinaMgk) decided:
- Kernel  Ring0
- Service Ring1 (aka. Driver)
- Module  Ring2
- Subapp  Ring3


## Routines for Flap-32

Gate Segment `8*03` and Interrupt 81H.

Rule
- Input
	- EAX syscall-id
	- ECX para 1
	- EDX para 2
	- EBX para 3
- Output
	- 

`enum class syscall_t`

| No. | Identification | Function            | IO        | Description            |
| --- | -------------- | ------------------- | --------- | ---------------------- |
| 0   | OUTC           | putchar current tty | (char)    |                        |
| 1   | INNC           | getchar             | ()→word   | 高字节包含键盘状态<br>TODO      |
| 2   | EXIT           | terminate program   | (code)    |                        |
| 3   | TIME           | syssecond           | ()→second | 获取秒数                   |
| 4   | REST           | haverest            |           | HLT 直到下一次中断发生          |
|     |                |                     |           |                        |
|     |                |                     |           |                        |
|     |                |                     |           |                        |
|     |                |                     |           |                        |
| FF  | TEST           | testing             | (T,E,S)   | 输入TES则反馈成功，并标注是哪个进程导致的 |




