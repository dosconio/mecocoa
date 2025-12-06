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
	- EAX

`enum class syscall_t`

| No. | Identification | Function  | IO                | Description                               |
| --- | -------------- | --------- | ----------------- | ----------------------------------------- |
| 00  | OUTC           |           | (char)            | putchar current tty                       |
| 01  | INNC           |           | (block_mode)→word | getchar<br>高字节包含键盘状态<br>**TODO**          |
| 02  | EXIT           |           | (code)            | terminate program<br>uncheck for new      |
| 03  | TIME           | syssecond | ()→second         | 获取秒数                                      |
| 04  | REST           | sysrest   |                   | HLT 直到下一次中断发生                             |
| 05  | COMM           | syscomm   |                   | communication: send/receive<br>同步(阻塞)收发信息 |
|     |                |           |                   |                                           |
|     |                |           |                   |                                           |
|     |                |           |                   |                                           |
| FF  | TEST           |           | ('T','E','S')→pid | 输入TES则反馈成功，并标注是哪个进程导致的                    |




