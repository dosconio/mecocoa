---
dg-publish: true
her-note: false
---

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

| Identification | Function            | IO        | Description            |
| -------------- | ------------------- | --------- | ---------------------- |
| OUTC           | putchar current tty | (char)    |                        |
| INNC           | getchar             | ()→word   | 高字节包含键盘状态<br>TODO      |
| EXIT           | terminate program   | (code)    |                        |
| TIME           | gettick             | ()→second | TODO                   |
| TEST           | testing             | (T,E,S)   | 输入TES则反馈成功，并标注是哪个进程导致的 |




